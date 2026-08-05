#pragma once

#include "domain/Models.h"
#include "infrastructure/database/IDatabase.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TicketHub::Application {

// Identity/session use cases for Phase 1 of the reduced-scope roadmap.
// There is no self-registration, no invitations, and no OIDC in V1 --
// createUser is an administrator-only action. See
// docs/REDUCED_SCOPE_SPECIFICATION.md section 3 and
// docs/REDUCED_SCOPE_ROADMAP.md Phase 1.
class AuthService {
public:
    explicit AuthService(std::shared_ptr<Infrastructure::Database::IDatabase> database);

    // Throws std::invalid_argument on validation failure (invalid email,
    // weak password, duplicate email).
    Domain::User createUser(Domain::CreateUserRequest request);

    // Throws Domain::AuthenticationFailed for any invalid-credential case
    // (unknown email, wrong password, deactivated account) -- these are
    // deliberately indistinguishable to the caller. Throws
    // Domain::AccountLocked if the minimal login-attempt lockout
    // (IDatabase::MaxFailedLoginAttempts) has tripped; the full configurable
    // policy is a Phase 6 addition.
    Domain::AuthenticatedSession login(Domain::LoginRequest request);

    // No-op (does not throw) if the token is already invalid/expired/unknown
    // -- logging out is always safe to call.
    void logout(const std::string& sessionToken);

    // nullopt if the token is missing, unknown, expired, or belongs to a
    // deactivated user.
    std::optional<Domain::Principal> validateSession(const std::string& sessionToken);

    // Personal access tokens (Phase 6, D39/D40): self-service only, no
    // admin-managed tokens. `expiresInDays` must be positive (D40 requires
    // an expiration; there is no "never expires" option). The raw token is
    // returned only in CreatedPersonalAccessToken, never again afterward.
    Domain::CreatedPersonalAccessToken createPersonalAccessToken(const std::string& userId,
                                                                  const std::string& name,
                                                                  int expiresInDays);
    std::vector<Domain::PersonalAccessToken> listPersonalAccessTokens(const std::string& userId);
    // False if the token doesn't exist, doesn't belong to userId, or is
    // already revoked -- ownership is enforced here, not left to the caller.
    bool revokePersonalAccessToken(const std::string& tokenId, const std::string& userId);
    // nullopt if the token is missing, unknown, expired, revoked, or
    // belongs to a deactivated user. Updates last_used_at on success.
    std::optional<Domain::Principal> validatePersonalAccessToken(const std::string& rawToken);

    // Active-session list and "sign out everywhere" (Phase 6, D54,
    // resequenced from Phase 1). currentSession resolves the session row
    // (not just the Principal) for a raw session token, so a caller can
    // identify which listed session is "this one." signOutOtherSessions
    // deliberately keeps currentSessionId active -- the request making the
    // call should never lock its own caller out -- and returns the number
    // of sessions removed.
    std::optional<Domain::Session> currentSession(const std::string& sessionToken);
    std::vector<Domain::Session> listActiveSessions(const std::string& userId);
    int signOutOtherSessions(const std::string& userId, const std::string& currentSessionId);

private:
    std::shared_ptr<Infrastructure::Database::IDatabase> database_;
};

} // namespace TicketHub::Application
