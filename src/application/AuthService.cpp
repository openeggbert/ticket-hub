#include "application/AuthService.h"

#include "common/PasswordHash.h"
#include "common/RandomToken.h"
#include "common/Sha256.h"
#include "common/Uuid.h"
#include "domain/Errors.h"
#include "domain/Validation.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace TicketHub::Application {
namespace {

// 30-day web session lifetime. There is no "remember me" distinction and no
// active-session list / sign-out-everywhere endpoint yet -- both are
// resequenced to Phase 6 (docs/REDUCED_SCOPE_ROADMAP.md).
constexpr long long SessionLifetimeSeconds = 30LL * 24 * 60 * 60;

Domain::Principal toPrincipal(const Domain::User& user) {
    return Domain::Principal{user.id, user.email, user.displayName, user.isAdmin, user.timeZone, user.clockFormat};
}

// Argon2id verification is deliberately slow; only running it on the
// "account exists" branch of login() would let a remote caller distinguish
// an unknown email from a wrong password purely by response time,
// enumerating valid accounts. Hashing a fixed dummy password once (lazily,
// thread-safe by C++11 static-local-init rules) gives the "account not
// found" branch something of comparable cost to verify against.
const std::string& dummyPasswordHashForTimingEqualization() {
    static const std::string hash = Common::hashPassword("th-dummy-password-for-timing-equalization");
    return hash;
}

void throwValidationErrors(const std::vector<std::string>& errors) {
    std::ostringstream message;
    for (std::size_t index = 0; index < errors.size(); ++index) {
        if (index != 0) {
            message << "; ";
        }
        message << errors[index];
    }
    throw std::invalid_argument(message.str());
}

} // namespace

AuthService::AuthService(std::shared_ptr<Infrastructure::Database::IDatabase> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("database must not be null");
    }
}

namespace {
Domain::User createUserAs(Infrastructure::Database::IDatabase& database,
                          Domain::CreateUserRequest request,
                          std::optional<std::string> actorUserId) {
    request.email = Domain::normalizeEmail(request.email);
    if (request.handle) {
        request.handle = Domain::normalizeHandle(*request.handle);
    }
    const auto errors = Domain::validateCreateUser(request);
    if (!errors.empty()) {
        throwValidationErrors(errors);
    }
    if (database.findUserByEmail(request.email).has_value()) {
        throw std::invalid_argument("Email is already in use: " + request.email);
    }
    if (request.handle && database.findUserByHandle(*request.handle).has_value()) {
        throw std::invalid_argument("Handle is already in use: " + *request.handle);
    }
    const std::string passwordHash = Common::hashPassword(request.password);
    const auto user = database.createUser(request, passwordHash);
    database.recordAuditEvent("identity", "user.created", actorUserId, std::string("user"), user.id, std::nullopt);
    return user;
}
} // namespace

Domain::User AuthService::createUser(Domain::CreateUserRequest request) {
    return createUserAs(*database_, std::move(request), std::nullopt);
}

Domain::AuthenticatedSession AuthService::login(Domain::LoginRequest request) {
    request.email = Domain::normalizeEmail(request.email);

    const auto user = database_->findUserByEmail(request.email);
    if (!user || !user->active) {
        // Deliberately identical to the wrong-password path below -- see
        // Domain::AuthenticationFailed's doc comment. Also deliberately
        // pays the same Argon2id verification cost as that path (see
        // dummyPasswordHashForTimingEqualization) so response time can't be
        // used to enumerate which emails have an account.
        Common::verifyPassword(dummyPasswordHashForTimingEqualization(), request.password);
        throw Domain::AuthenticationFailed("Invalid email or password");
    }

    if (database_->isLoginLocked(user->id)) {
        database_->recordAuditEvent("auth", "login.blocked", std::nullopt, std::string("user"), user->id, std::nullopt);
        throw Domain::AccountLocked("Too many failed login attempts; try again later");
    }

    const auto passwordHash = database_->findPasswordHash(user->id);
    if (!passwordHash || !Common::verifyPassword(*passwordHash, request.password)) {
        database_->recordFailedLogin(user->id);
        database_->recordAuditEvent("auth", "login.failed", std::nullopt, std::string("user"), user->id, std::nullopt);
        throw Domain::AuthenticationFailed("Invalid email or password");
    }

    database_->resetFailedLogin(user->id);

    const std::string token = Common::randomTokenHex(32);
    const std::string tokenHash = Common::sha256Hex(token);
    const std::string expiresAt = Common::utcNowPlusSecondsIso8601(SessionLifetimeSeconds);
    const auto session = database_->createSession(user->id, tokenHash, expiresAt);

    return Domain::AuthenticatedSession{session, token};
}

void AuthService::logout(const std::string& sessionToken) {
    const std::string tokenHash = Common::sha256Hex(sessionToken);
    const auto session = database_->findSessionByTokenHash(tokenHash);
    if (session) {
        database_->deleteSession(session->id);
    }
}

std::optional<Domain::Principal> AuthService::validateSession(const std::string& sessionToken) {
    if (sessionToken.empty()) {
        return std::nullopt;
    }
    const std::string tokenHash = Common::sha256Hex(sessionToken);
    const auto session = database_->findSessionByTokenHash(tokenHash);
    if (!session) {
        return std::nullopt;
    }
    const auto user = database_->findUserById(session->userId);
    if (!user || !user->active) {
        return std::nullopt;
    }
    return toPrincipal(*user);
}

Domain::CreatedPersonalAccessToken AuthService::createPersonalAccessToken(const std::string& userId,
                                                                          const std::string& name,
                                                                          const int expiresInDays) {
    if (name.empty()) {
        throw std::invalid_argument("Token name is required");
    }
    if (expiresInDays <= 0) {
        throw std::invalid_argument("expiresInDays must be positive");
    }
    const std::string rawToken = Common::randomTokenHex(32);
    const std::string tokenHash = Common::sha256Hex(rawToken);
    const std::string expiresAt = Common::utcNowPlusSecondsIso8601(static_cast<long long>(expiresInDays) * 24 * 60 * 60);
    const auto token = database_->createPersonalAccessToken(userId, name, tokenHash, expiresAt);
    return Domain::CreatedPersonalAccessToken{token, rawToken};
}

std::vector<Domain::PersonalAccessToken> AuthService::listPersonalAccessTokens(const std::string& userId) {
    return database_->listPersonalAccessTokens(userId);
}

bool AuthService::revokePersonalAccessToken(const std::string& tokenId, const std::string& userId) {
    return database_->revokePersonalAccessToken(tokenId, userId);
}

std::optional<Domain::Principal> AuthService::validatePersonalAccessToken(const std::string& rawToken) {
    if (rawToken.empty()) {
        return std::nullopt;
    }
    const std::string tokenHash = Common::sha256Hex(rawToken);
    const auto token = database_->findPersonalAccessTokenByHash(tokenHash);
    if (!token) {
        return std::nullopt;
    }
    const auto user = database_->findUserById(token->userId);
    if (!user || !user->active) {
        return std::nullopt;
    }
    database_->touchPersonalAccessTokenLastUsed(token->id);
    return toPrincipal(*user);
}

void AuthService::changeOwnPassword(const Domain::Principal& actor,
                                    const std::string& currentPassword,
                                    const std::string& newPassword,
                                    const std::string& keepSessionId) {
    const auto storedHash = database_->findPasswordHash(actor.userId);
    if (!storedHash || !Common::verifyPassword(*storedHash, currentPassword)) {
        // Same message and same exception type as a failed login: a caller
        // who guesses wrong learns nothing beyond "that was not it".
        throw Domain::AuthenticationFailed("Current password is incorrect");
    }
    if (newPassword == currentPassword) {
        throw std::invalid_argument("New password must be different from the current password");
    }
    const auto errors = Domain::validatePassword(newPassword, actor.email, actor.displayName);
    if (!errors.empty()) {
        throwValidationErrors(errors);
    }

    database_->setPasswordHash(actor.userId, Common::hashPassword(newPassword));
    // Clear any failed-login lockout: someone who just proved they know the
    // current password should not stay locked out by earlier wrong guesses.
    database_->resetFailedLogin(actor.userId);
    // D53's "session invalidation after password change" -- every other
    // session is dropped, so a password change genuinely evicts anyone else
    // holding a stolen session token. The caller's own session survives.
    database_->deleteOtherSessionsForUser(actor.userId, keepSessionId);
    database_->recordAuditEvent("identity", "user.password_changed", actor.userId, std::string("user"),
                                actor.userId, std::nullopt);
}

std::optional<Domain::Session> AuthService::currentSession(const std::string& sessionToken) {
    if (sessionToken.empty()) {
        return std::nullopt;
    }
    return database_->findSessionByTokenHash(Common::sha256Hex(sessionToken));
}

std::vector<Domain::Session> AuthService::listActiveSessions(const std::string& userId) {
    return database_->listSessionsForUser(userId);
}

int AuthService::signOutOtherSessions(const std::string& userId, const std::string& currentSessionId) {
    return database_->deleteOtherSessionsForUser(userId, currentSessionId);
}

void AuthService::requireGlobalAdmin(const Domain::Principal& actor) const {
    if (!actor.isAdmin) {
        throw Domain::Forbidden("This action requires global administrator privileges");
    }
}

std::vector<Domain::User> AuthService::adminListUsers(const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->listUsers();
}

Domain::User AuthService::adminCreateUser(Domain::CreateUserRequest request, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return createUserAs(*database_, std::move(request), actor.userId);
}

bool AuthService::adminSetUserActive(const std::string& userId, const bool active, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    if (userId == actor.userId) {
        throw std::invalid_argument("You cannot deactivate your own account");
    }
    const bool changed = database_->setUserActive(userId, active);
    if (changed) {
        database_->deleteAllSessionsForUser(userId);
        database_->recordAuditEvent("identity", active ? "user.activated" : "user.deactivated", actor.userId,
                                    std::string("user"), userId, std::nullopt);
    }
    return changed;
}

bool AuthService::adminSetUserAdmin(const std::string& userId, const bool isAdmin, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    if (userId == actor.userId && !isAdmin) {
        throw std::invalid_argument("You cannot remove your own global administrator privileges");
    }
    const bool changed = database_->setUserAdmin(userId, isAdmin);
    if (changed) {
        database_->recordAuditEvent("identity", isAdmin ? "user.admin_granted" : "user.admin_revoked", actor.userId,
                                    std::string("user"), userId, std::nullopt);
    }
    return changed;
}

std::optional<std::string> AuthService::adminResetPassword(const std::string& userId, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    if (!database_->findUserById(userId).has_value()) {
        return std::nullopt;
    }
    const std::string temporaryPassword = Common::randomTokenHex(16);
    database_->setPasswordHash(userId, Common::hashPassword(temporaryPassword));
    database_->deleteAllSessionsForUser(userId);
    database_->recordAuditEvent("identity", "user.password_reset_by_admin", actor.userId, std::string("user"), userId,
                                std::nullopt);
    return temporaryPassword;
}

} // namespace TicketHub::Application
