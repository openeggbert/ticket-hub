#include "application/AuthService.h"
#include "domain/Errors.h"
#include "infrastructure/database/SqliteDatabase.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifndef TICKETHUB_SOURCE_DIR
#define TICKETHUB_SOURCE_DIR "."
#endif

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Backdates a login lock so the "the window has elapsed" case is testable in
// milliseconds instead of 15 real minutes. Done by reaching into the SQLite
// file directly rather than adding a test-only method to `IDatabase`, which
// would put a production-interface hole in every adapter for the benefit of
// one test.
void expireLoginLock(const std::string& databasePath, const std::string& userId) {
    sqlite3* handle = nullptr;
    require(sqlite3_open(databasePath.c_str(), &handle) == SQLITE_OK, "test helper can open the database");
    const std::string sql =
        "UPDATE local_credentials SET locked_until = datetime('now', '-1 minute') WHERE user_id = '" + userId + "'";
    char* error = nullptr;
    const int result = sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &error);
    if (error != nullptr) {
        sqlite3_free(error);
    }
    sqlite3_close(handle);
    require(result == SQLITE_OK, "test helper can backdate locked_until");
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using TicketHub::Application::AuthService;
    using TicketHub::Domain::AccountLocked;
    using TicketHub::Domain::AuthenticationFailed;
    using TicketHub::Domain::CreateUserRequest;
    using TicketHub::Domain::LoginRequest;
    using TicketHub::Infrastructure::Database::IDatabase;
    using TicketHub::Infrastructure::Database::SqliteDatabase;

    const fs::path sourceRoot(TICKETHUB_SOURCE_DIR);
    const fs::path databasePath = fs::temp_directory_path() / "ticket-hub-identity.db";
    std::error_code removeError;
    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    auto database = std::make_shared<SqliteDatabase>(
        databasePath.string(),
        (sourceRoot / "migrations/sqlite").string(),
        (sourceRoot / "migrations/sqlite/002_seed_demo.sql").string());
    database->migrate();
    database->seedDemoData();

    AuthService auth(database);

    // --- Seeded demo account (validates the seed's Argon2id hash too) ---
    {
        auto session = auth.login(LoginRequest{"demo@ticket-hub.local", "demo12345"});
        require(!session.sessionToken.empty(), "login returns a non-empty raw session token");
        require(session.session.userId == "00000000-0000-4000-8000-000000000001",
               "login resolves to the seeded demo user id");

        const auto principal = auth.validateSession(session.sessionToken);
        require(principal.has_value(), "a fresh session token validates");
        require(principal->email == "demo@ticket-hub.local", "validated principal has the expected email");
        require(principal->isAdmin, "seeded demo account is an administrator");

        auth.logout(session.sessionToken);
        require(!auth.validateSession(session.sessionToken).has_value(), "session is invalid after logout");
        // Logout is idempotent.
        auth.logout(session.sessionToken);
    }

    require(!auth.validateSession("not-a-real-token").has_value(), "an unknown token never validates");
    require(!auth.validateSession("").has_value(), "an empty token never validates");

    // --- Wrong password / unknown email are indistinguishable ---
    bool wrongPasswordFailed = false;
    try {
        auth.login(LoginRequest{"demo@ticket-hub.local", "definitely-wrong"});
    } catch (const AuthenticationFailed&) {
        wrongPasswordFailed = true;
    }
    require(wrongPasswordFailed, "wrong password throws AuthenticationFailed");

    bool unknownEmailFailed = false;
    std::string unknownMessage;
    try {
        auth.login(LoginRequest{"nobody@ticket-hub.local", "whatever-password"});
    } catch (const AuthenticationFailed& error) {
        unknownEmailFailed = true;
        unknownMessage = error.what();
    }
    require(unknownEmailFailed, "unknown email throws AuthenticationFailed");
    bool wrongPasswordFailedAgain = false;
    std::string wrongPasswordMessage;
    try {
        auth.login(LoginRequest{"demo@ticket-hub.local", "definitely-wrong"});
    } catch (const AuthenticationFailed& error) {
        wrongPasswordFailedAgain = true;
        wrongPasswordMessage = error.what();
    }
    require(wrongPasswordFailedAgain, "wrong password (second check) throws AuthenticationFailed");
    require(unknownMessage == wrongPasswordMessage,
           "unknown-email and wrong-password messages are identical (no user-enumeration oracle)");

    // --- Minimal lockout: alex@ticket-hub.local, untouched by the demo-user
    // failed attempts above, so its counter starts fresh. ---
    {
        bool lockedOut = false;
        for (int attempt = 0; attempt < IDatabase::MaxFailedLoginAttempts + 1 && !lockedOut; ++attempt) {
            try {
                auth.login(LoginRequest{"alex@ticket-hub.local", "wrong-password"});
            } catch (const AccountLocked&) {
                lockedOut = true;
            } catch (const AuthenticationFailed&) {
                // Expected for the first MaxFailedLoginAttempts attempts.
            }
        }
        require(lockedOut, "account locks after MaxFailedLoginAttempts failures");

        bool lockedEvenWithCorrectPassword = false;
        try {
            auth.login(LoginRequest{"alex@ticket-hub.local", "demo12345"});
        } catch (const AccountLocked&) {
            lockedEvenWithCorrectPassword = true;
        }
        require(lockedEvenWithCorrectPassword, "correct password is still rejected while locked");
    }

    // --- Administrator-created accounts (the entire V1 registration story) ---
    {
        CreateUserRequest request;
        request.email = "New.User@Ticket-Hub.Local";
        request.displayName = "New User";
        request.password = "correct horse battery staple";
        request.isAdmin = false;

        const auto user = auth.createUser(request);
        require(user.email == "new.user@ticket-hub.local", "created user's email is normalized to lowercase");
        require(!user.isAdmin, "created user is not an administrator by default");
        require(!user.id.empty(), "created user has an id");

        auto session = auth.login(LoginRequest{"new.user@ticket-hub.local", "correct horse battery staple"});
        const auto principal = auth.validateSession(session.sessionToken);
        require(principal.has_value() && principal->userId == user.id,
               "the new user can immediately log in with the password the admin set");

        bool duplicateRejected = false;
        try {
            auth.createUser(request);
        } catch (const std::invalid_argument&) {
            duplicateRejected = true;
        }
        require(duplicateRejected, "duplicate email is rejected");

        CreateUserRequest invalid;
        invalid.email = "not-an-email";
        invalid.displayName = "Someone";
        invalid.password = "short";
        bool invalidRejected = false;
        try {
            auth.createUser(invalid);
        } catch (const std::invalid_argument&) {
            invalidRejected = true;
        }
        require(invalidRejected, "invalid create-user request is rejected");
    }

    // --- @mention handles (D56/D80) ---
    {
        CreateUserRequest withHandle;
        withHandle.email = "handle.user@ticket-hub.local";
        withHandle.displayName = "Handle User";
        withHandle.password = "correct horse battery staple";
        withHandle.handle = "Handle_User";

        const auto user = auth.createUser(withHandle);
        require(user.handle.has_value() && *user.handle == "handle_user",
               "the handle is normalized to lowercase, same as email");

        CreateUserRequest duplicateHandle;
        duplicateHandle.email = "another.user@ticket-hub.local";
        duplicateHandle.displayName = "Another User";
        duplicateHandle.password = "correct horse battery staple";
        duplicateHandle.handle = "HANDLE_USER"; // same handle, different case
        bool duplicateHandleRejected = false;
        try {
            auth.createUser(duplicateHandle);
        } catch (const std::invalid_argument&) {
            duplicateHandleRejected = true;
        }
        require(duplicateHandleRejected, "a duplicate handle is rejected even with different casing");

        CreateUserRequest invalidHandle;
        invalidHandle.email = "invalid.handle@ticket-hub.local";
        invalidHandle.displayName = "Invalid Handle";
        invalidHandle.password = "correct horse battery staple";
        invalidHandle.handle = "not a valid handle!";
        bool invalidHandleRejected = false;
        try {
            auth.createUser(invalidHandle);
        } catch (const std::invalid_argument&) {
            invalidHandleRejected = true;
        }
        require(invalidHandleRejected, "a handle with spaces/punctuation is rejected");

        CreateUserRequest noHandle;
        noHandle.email = "no.handle@ticket-hub.local";
        noHandle.displayName = "No Handle";
        noHandle.password = "correct horse battery staple";
        const auto userWithoutHandle = auth.createUser(noHandle);
        require(!userWithoutHandle.handle.has_value(), "the handle remains optional -- omitting it is not an error");
    }

    // --- Active-session list and "sign out everywhere" (Phase 6, D54) ---
    {
        const std::string demoUserId = "00000000-0000-4000-8000-000000000001";
        const LoginRequest demoLogin{"demo@ticket-hub.local", "demo12345"};

        const auto sessionA = auth.login(demoLogin);
        const auto sessionB = auth.login(demoLogin);
        const auto sessionC = auth.login(demoLogin);

        const auto resolvedA = auth.currentSession(sessionA.sessionToken);
        require(resolvedA.has_value() && resolvedA->id == sessionA.session.id,
               "currentSession resolves the session row (not just the Principal) for a raw token");
        require(!auth.currentSession("not-a-real-token").has_value(), "currentSession is nullopt for an unknown token");

        const auto active = auth.listActiveSessions(demoUserId);
        require(active.size() >= 3, "listActiveSessions includes all three freshly created sessions");
        require(std::any_of(active.begin(), active.end(), [&](const auto& s) { return s.id == sessionA.session.id; })
                    && std::any_of(active.begin(), active.end(), [&](const auto& s) { return s.id == sessionB.session.id; })
                    && std::any_of(active.begin(), active.end(), [&](const auto& s) { return s.id == sessionC.session.id; }),
               "all three sessions are present in the list");

        const int removed = auth.signOutOtherSessions(demoUserId, sessionA.session.id);
        require(removed >= 2, "signOutOtherSessions removes every other session for the user");
        require(auth.validateSession(sessionA.sessionToken).has_value(),
               "signOutOtherSessions keeps the caller's own current session active");
        require(!auth.validateSession(sessionB.sessionToken).has_value(),
               "a different session for the same user is signed out");
        require(!auth.validateSession(sessionC.sessionToken).has_value(),
               "every other session for the same user is signed out");

        const auto afterSignOut = auth.listActiveSessions(demoUserId);
        require(afterSignOut.size() == 1 && afterSignOut[0].id == sessionA.session.id,
               "only the caller's own session remains listed");

        auth.logout(sessionA.sessionToken);
    }

    // --- Personal access tokens (Phase 6, D39/D40) ---
    {
        const std::string demoUserId = "00000000-0000-4000-8000-000000000001";
        const std::string alexUserId = "00000000-0000-4000-8000-000000000002";

        const auto created = auth.createPersonalAccessToken(demoUserId, "CI script", 30);
        require(!created.rawToken.empty(), "creating a token returns a non-empty raw token");
        require(created.token.name == "CI script", "the token name round-trips");
        require(!created.token.revokedAt.has_value() && !created.token.lastUsedAt.has_value(),
               "a freshly created token is not revoked and has never been used");

        const auto principal = auth.validatePersonalAccessToken(created.rawToken);
        require(principal.has_value() && principal->userId == demoUserId,
               "a fresh PAT validates and resolves to its owner");
        require(!auth.validatePersonalAccessToken("not-a-real-token").has_value(), "an unknown PAT never validates");
        require(!auth.validatePersonalAccessToken("").has_value(), "an empty PAT never validates");

        const auto afterUse = auth.listPersonalAccessTokens(demoUserId);
        const auto match = std::find_if(afterUse.begin(), afterUse.end(),
                                        [&](const auto& t) { return t.id == created.token.id; });
        require(match != afterUse.end() && match->lastUsedAt.has_value(),
               "validating a PAT updates its last_used_at timestamp");

        require(!auth.revokePersonalAccessToken(created.token.id, alexUserId),
               "revoking someone else's token fails (ownership is enforced)");
        require(auth.validatePersonalAccessToken(created.rawToken).has_value(),
               "a token survives a failed revoke attempt by a non-owner");

        require(auth.revokePersonalAccessToken(created.token.id, demoUserId), "the owner can revoke their own token");
        require(!auth.validatePersonalAccessToken(created.rawToken).has_value(),
               "a revoked token no longer validates");
        require(!auth.revokePersonalAccessToken(created.token.id, demoUserId), "revoking an already-revoked token is a no-op");

        bool zeroExpiryRejected = false;
        try {
            auth.createPersonalAccessToken(demoUserId, "bad", 0);
        } catch (const std::invalid_argument&) {
            zeroExpiryRejected = true;
        }
        require(zeroExpiryRejected, "a non-positive expiresInDays is rejected -- D40 requires an expiration");

        bool emptyNameRejected = false;
        try {
            auth.createPersonalAccessToken(demoUserId, "", 30);
        } catch (const std::invalid_argument&) {
            emptyNameRejected = true;
        }
        require(emptyNameRejected, "an empty token name is rejected");
    }

    // --- Simple append-only admin/security audit log (D23) ---
    // --- Self-service password change (security audit 2026-08-26, M1) ---
    {
        CreateUserRequest request;
        request.email = "changer@example.com";
        request.displayName = "Pat Changer";
        request.password = "original-password-1";
        const auto user = auth.createUser(request);

        auto first = auth.login(LoginRequest{"changer@example.com", "original-password-1"});
        auto second = auth.login(LoginRequest{"changer@example.com", "original-password-1"});
        const auto principal = auth.validateSession(second.sessionToken);
        require(principal.has_value(), "the second session validates before the password change");
        require(auth.listActiveSessions(user.id).size() == 2, "the user now holds two sessions");

        // A wrong current password must not change anything, even though the
        // caller already holds a valid session: a borrowed session token
        // cannot be escalated into permanent account ownership.
        bool rejected = false;
        try {
            auth.changeOwnPassword(*principal, "not-the-password", "brand-new-password-2",
                                   second.session.id);
        } catch (const AuthenticationFailed&) {
            rejected = true;
        }
        require(rejected, "changing a password with the wrong current password is rejected");
        require(auth.validateSession(first.sessionToken).has_value(),
                "a rejected change leaves other sessions alone");

        // A weak new password is rejected by the same rules as account creation.
        bool weakRejected = false;
        try {
            auth.changeOwnPassword(*principal, "original-password-1", "short", second.session.id);
        } catch (const std::invalid_argument&) {
            weakRejected = true;
        }
        require(weakRejected, "a new password below the minimum length is rejected");

        bool sameRejected = false;
        try {
            auth.changeOwnPassword(*principal, "original-password-1", "original-password-1",
                                   second.session.id);
        } catch (const std::invalid_argument&) {
            sameRejected = true;
        }
        require(sameRejected, "reusing the current password as the new password is rejected");

        auth.changeOwnPassword(*principal, "original-password-1", "brand-new-password-2",
                               second.session.id);

        bool oldPasswordRejected = false;
        try {
            auth.login(LoginRequest{"changer@example.com", "original-password-1"});
        } catch (const AuthenticationFailed&) {
            oldPasswordRejected = true;
        }
        require(oldPasswordRejected, "the old password no longer authenticates");

        auto reloggedIn = auth.login(LoginRequest{"changer@example.com", "brand-new-password-2"});
        require(!reloggedIn.sessionToken.empty(), "the new password authenticates");

        // D53's "session invalidation after password change": every other
        // session is dropped, the caller's own survives.
        require(!auth.validateSession(first.sessionToken).has_value(),
                "the user's other session is invalidated by the password change");
        require(auth.validateSession(second.sessionToken).has_value(),
                "the session that performed the change stays signed in");

        const auto events = database->listAuditEvents(500);
        require(std::any_of(events.begin(), events.end(),
                            [&user](const auto& e) {
                                return e.category == "identity" && e.action == "user.password_changed" &&
                                       e.actor.has_value() && e.actor->id == user.id;
                            }),
               "a self-service password change records an identity/user.password_changed event");
    }

    // --- Login lockout expires instead of latching (security audit, H3) ---
    {
        CreateUserRequest request;
        request.email = "lockout@example.com";
        request.displayName = "Lee Lockout";
        request.password = "lockout-password-1";
        const auto user = auth.createUser(request);

        for (int attempt = 0; attempt < IDatabase::MaxFailedLoginAttempts; ++attempt) {
            try {
                auth.login(LoginRequest{"lockout@example.com", "wrong-password"});
            } catch (const AuthenticationFailed&) {
                // expected
            }
        }
        require(database->isLoginLocked(user.id), "the account locks after MaxFailedLoginAttempts");

        // Simulate the lock window elapsing. Before the H3 fix,
        // failed_login_count stayed at the maximum forever, so the very next
        // wrong password re-locked the account -- one wrong guess every 15
        // minutes kept a named account locked out indefinitely, with no
        // self-service reset to recover through.
        expireLoginLock(databasePath.string(), user.id);
        require(!database->isLoginLocked(user.id), "the lock is no longer active once its window has passed");

        try {
            auth.login(LoginRequest{"lockout@example.com", "wrong-password"});
        } catch (const AuthenticationFailed&) {
            // expected
        }
        require(!database->isLoginLocked(user.id),
                "a single wrong password after the lock expired does NOT immediately re-lock (H3)");

        auto session = auth.login(LoginRequest{"lockout@example.com", "lockout-password-1"});
        require(!session.sessionToken.empty(), "the correct password works once the lock has expired");
    }

    // Every login-failed/login-blocked/user-created event from the blocks
    // above should already have been recorded by this point.
    {
        const auto events = database->listAuditEvents(500);
        require(std::any_of(events.begin(), events.end(),
                            [](const auto& e) { return e.category == "auth" && e.action == "login.failed"; }),
               "a failed login records an auth/login.failed audit event");
        require(std::any_of(events.begin(), events.end(),
                            [](const auto& e) { return e.category == "auth" && e.action == "login.blocked"; }),
               "a blocked (locked-out) login records an auth/login.blocked audit event");
        require(std::any_of(events.begin(), events.end(),
                            [](const auto& e) {
                                return e.category == "identity" && e.action == "user.created" && !e.actor.has_value();
                            }),
               "a CLI-driven user creation records an identity/user.created event with no actor "
               "(create-user runs outside any web session)");
    }

    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    std::cout << "Identity integration tests passed\n";
    return 0;
}
