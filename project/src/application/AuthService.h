#pragma once

#include "domain/Models.h"
#include "infrastructure/database/IDatabase.h"

#include <memory>
#include <optional>
#include <string>

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

private:
    std::shared_ptr<Infrastructure::Database::IDatabase> database_;
};

} // namespace TicketHub::Application
