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

    // Administrator-only account management (D2/D53/D57): there is still no
    // self-service registration/invitation/reset-by-email in V1 -- these are
    // the same create/deactivate/reset actions the CLI already performs
    // (createUser above; ticket-hub-cli has no deactivate/reset equivalent
    // yet), now reachable by a logged-in global administrator over the web,
    // with the actor checked here rather than in the Crow route (this
    // codebase centralizes authorization in the service layer). Every
    // method throws Domain::Forbidden if `actor` is not a global admin,
    // exactly like TicketService::requireGlobalAdmin-gated actions, so the
    // existing Api.cpp `catch (const Domain::Forbidden&)` -> 403 pattern
    // applies unchanged.
    std::vector<Domain::User> adminListUsers(const Domain::Principal& actor);
    // Same validation/duplicate-email/handle checks as createUser (shares
    // its implementation) but attributes the audit event to `actor` instead
    // of leaving it actor-less like the CLI path.
    Domain::User adminCreateUser(Domain::CreateUserRequest request, const Domain::Principal& actor);
    // False if userId does not resolve to an existing user. Throws
    // std::invalid_argument if userId == actor.userId -- an admin
    // deactivating their own account is never useful and risks a confusing
    // self-lockout, so it is rejected outright rather than merely
    // discouraged. Deactivating also kills every existing session for that
    // user (D57's deactivation should take effect immediately, not just on
    // their next session validation).
    bool adminSetUserActive(const std::string& userId, bool active, const Domain::Principal& actor);
    // False if userId does not resolve to an existing user. Throws
    // std::invalid_argument if userId == actor.userId and isAdmin is false
    // -- an admin can promote anyone (including making a second admin) but
    // can never demote themselves through this action, the same
    // never-lock-yourself-out reasoning as adminSetUserActive.
    bool adminSetUserAdmin(const std::string& userId, bool isAdmin, const Domain::Principal& actor);
    // D53: "admin-performed reset (sets a temporary password)" -- replaces
    // the email-based reset original scope assumed self-service email
    // (Decisions 2/52 removed both). Generates and returns a fresh random
    // temporary password (shown to the calling admin exactly once, like a
    // PAT's raw token -- it is never stored or retrievable again, only its
    // hash), clears any failed-login lockout, and invalidates every
    // existing session for that user (session invalidation after a
    // password change, also part of D53). Returns nullopt if userId does
    // not resolve to an existing user.
    std::optional<std::string> adminResetPassword(const std::string& userId, const Domain::Principal& actor);

private:
    void requireGlobalAdmin(const Domain::Principal& actor) const;

    std::shared_ptr<Infrastructure::Database::IDatabase> database_;
};

} // namespace TicketHub::Application
