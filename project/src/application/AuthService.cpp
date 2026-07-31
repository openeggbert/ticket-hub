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
    return Domain::Principal{user.id, user.email, user.displayName, user.isAdmin};
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

Domain::User AuthService::createUser(Domain::CreateUserRequest request) {
    request.email = Domain::normalizeEmail(request.email);
    if (request.handle) {
        request.handle = Domain::normalizeHandle(*request.handle);
    }
    const auto errors = Domain::validateCreateUser(request);
    if (!errors.empty()) {
        throwValidationErrors(errors);
    }
    if (database_->findUserByEmail(request.email).has_value()) {
        throw std::invalid_argument("Email is already in use: " + request.email);
    }
    if (request.handle && database_->findUserByHandle(*request.handle).has_value()) {
        throw std::invalid_argument("Handle is already in use: " + *request.handle);
    }
    const std::string passwordHash = Common::hashPassword(request.password);
    return database_->createUser(request, passwordHash);
}

Domain::AuthenticatedSession AuthService::login(Domain::LoginRequest request) {
    request.email = Domain::normalizeEmail(request.email);

    const auto user = database_->findUserByEmail(request.email);
    if (!user || !user->active) {
        // Deliberately identical to the wrong-password path below -- see
        // Domain::AuthenticationFailed's doc comment.
        throw Domain::AuthenticationFailed("Invalid email or password");
    }

    if (database_->isLoginLocked(user->id)) {
        throw Domain::AccountLocked("Too many failed login attempts; try again later");
    }

    const auto passwordHash = database_->findPasswordHash(user->id);
    if (!passwordHash || !Common::verifyPassword(*passwordHash, request.password)) {
        database_->recordFailedLogin(user->id);
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

} // namespace TicketHub::Application
