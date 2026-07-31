#include "application/AuthService.h"
#include "domain/Errors.h"
#include "infrastructure/database/SqliteDatabase.h"

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

    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    std::cout << "Identity integration tests passed\n";
    return 0;
}
