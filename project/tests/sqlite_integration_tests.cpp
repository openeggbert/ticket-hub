#include "infrastructure/database/SqliteDatabase.h"
#include "domain/Errors.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <fstream>
#include <stdexcept>
#include <sqlite3.h>

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

void executeSql(const std::filesystem::path& databasePath, const std::string& sql) {
    sqlite3* database = nullptr;
    if (sqlite3_open(databasePath.string().c_str(), &database) != SQLITE_OK) {
        throw std::runtime_error("cannot open SQLite test database");
    }
    char* error = nullptr;
    const int result = sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &error);
    const std::string message = error == nullptr ? std::string{} : std::string(error);
    sqlite3_free(error);
    sqlite3_close(database);
    if (result != SQLITE_OK) {
        throw std::runtime_error("SQLite test statement failed: " + message);
    }
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using TicketHub::Domain::CreateIssueRequest;
    using TicketHub::Domain::IssueFilter;
    using TicketHub::Infrastructure::Database::SqliteDatabase;

    const fs::path sourceRoot(TICKETHUB_SOURCE_DIR);
    const fs::path databasePath = fs::temp_directory_path() / "ticket-hub-integration.db";
    std::error_code removeError;
    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    {
        SqliteDatabase database(
            databasePath.string(),
            (sourceRoot / "migrations/sqlite").string(),
            (sourceRoot / "migrations/sqlite/002_seed_demo.sql").string());

        database.migrate();
        database.seedDemoData();
        database.seedDemoData();

        const auto projects = database.listProjects();
        require(projects.size() == 2, "demo seed creates two projects and is idempotent");

        const auto initialIssues = database.listIssues(IssueFilter{});
        require(initialIssues.size() == 8, "demo seed creates eight issues");

        CreateIssueRequest request;
        request.projectKey = "TH";
        request.summary = "Verify portable database architecture";
        request.description = "Created by the SQLite integration test.";
        request.issueTypeKey = "task";
        request.priorityKey = "high";
        request.assigneeUsername = "alex";
        request.labels = {"database", "integration"};
        request.storyPoints = 3.0;

        const auto created = database.createIssue(request, "demo");
        require(created.key == "TH-7", "project-local issue counter creates TH-7");
        require(created.labels.size() == 2, "created issue has both labels");

        const auto found = database.findIssueByKey(created.key);
        require(found.has_value() && found->summary == request.summary, "created issue can be read back");

        executeSql(databasePath,
                   "INSERT INTO issue_key_aliases(alias_key, issue_id) VALUES ('LEGACY-7', '" + created.id + "')");
        const auto viaAlias = database.findIssueByKey("LEGACY-7");
        require(viaAlias.has_value() && viaAlias->key == created.key, "permanent issue-key alias resolves to current key");

        require(database.changeIssueStatus(created.key, "done", "demo", created.version), "issue status can be changed");
        const auto done = database.findIssueByKey(created.key);
        require(done.has_value() && done->status.key == "done", "changed status is persisted");
        require(done->version == created.version + 1, "status change increments optimistic-lock version");
        bool conflictDetected = false;
        try {
            database.changeIssueStatus(created.key, "review", "demo", created.version);
        } catch (const TicketHub::Domain::ConcurrencyConflict&) {
            conflictDetected = true;
        }
        require(conflictDetected, "stale status update is rejected");

        const auto comment = database.addComment({created.key, "Database adapter smoke test passed."}, "demo");
        require(!comment.id.empty(), "comment receives an id");
        require(database.listComments(created.key).size() == 1, "comment can be listed");

        const auto dashboard = database.dashboardStats();
        require(dashboard.totalIssues == 9, "dashboard includes newly created issue");
        require(!dashboard.recentIssues.empty() && dashboard.recentIssues.front().key == created.key,
                "dashboard returns the most recently updated issue first");
    }

    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    const fs::path checksumDatabase = fs::temp_directory_path() / "ticket-hub-migration-checksum.db";
    const fs::path migrationCopy = fs::temp_directory_path() / "ticket-hub-migration-copy";
    fs::remove(checksumDatabase, removeError);
    fs::remove_all(migrationCopy, removeError);
    fs::create_directories(migrationCopy);
    fs::copy_file(sourceRoot / "migrations/sqlite/001_initial.sql", migrationCopy / "001_initial.sql");
    fs::copy_file(sourceRoot / "migrations/sqlite/003_product_foundation.sql", migrationCopy / "003_product_foundation.sql");
    {
        SqliteDatabase database(checksumDatabase.string(), migrationCopy.string(),
                                (sourceRoot / "migrations/sqlite/002_seed_demo.sql").string());
        database.migrate();
        std::ofstream(migrationCopy / "003_product_foundation.sql", std::ios::app) << "\n-- modified after apply\n";
        bool mismatchDetected = false;
        try {
            database.migrate();
        } catch (const std::runtime_error&) {
            mismatchDetected = true;
        }
        require(mismatchDetected, "changed historical migration is rejected");
    }
    fs::remove(checksumDatabase, removeError);
    fs::remove(checksumDatabase.string() + "-wal", removeError);
    fs::remove(checksumDatabase.string() + "-shm", removeError);
    fs::remove_all(migrationCopy, removeError);

    std::cout << "SQLite integration tests passed\n";
    return 0;
}
