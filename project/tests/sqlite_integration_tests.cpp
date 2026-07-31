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

int scalarInt(const std::filesystem::path& databasePath, const std::string& sql) {
    sqlite3* database = nullptr;
    if (sqlite3_open(databasePath.string().c_str(), &database) != SQLITE_OK) {
        throw std::runtime_error("cannot open SQLite test database");
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        sqlite3_close(database);
        throw std::runtime_error("cannot prepare SQLite test query");
    }
    int value = 0;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        value = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return value;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using TicketHub::Domain::CreateIssueRequest;
    using TicketHub::Domain::IssueFilter;
    using TicketHub::Infrastructure::Database::SqliteDatabase;

    // Fixed seed identity from migrations/sqlite/002_seed_demo.sql.
    const std::string demoUserId = "00000000-0000-4000-8000-000000000001";

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
        request.assigneeEmail = "alex@ticket-hub.local";
        request.labels = {"database", "integration"};
        request.storyPoints = 3.0;

        const auto created = database.createIssue(request, demoUserId);
        require(created.key == "TH-7", "project-local issue counter creates TH-7");
        require(created.labels.size() == 2, "created issue has both labels");

        const auto found = database.findIssueByKey(created.key);
        require(found.has_value() && found->summary == request.summary, "created issue can be read back");

        executeSql(databasePath,
                   "INSERT INTO issue_key_aliases(alias_key, issue_id) VALUES ('LEGACY-7', '" + created.id + "')");
        const auto viaAlias = database.findIssueByKey("LEGACY-7");
        require(viaAlias.has_value() && viaAlias->key == created.key, "permanent issue-key alias resolves to current key");

        require(database.changeIssueStatus(created.key, "done", demoUserId, std::string("fixed"), created.version),
               "issue status can be changed");
        const auto done = database.findIssueByKey(created.key);
        require(done.has_value() && done->status.key == "done", "changed status is persisted");
        require(done->version == created.version + 1, "status change increments optimistic-lock version");
        require(done->resolution.has_value() && *done->resolution == "fixed",
               "resolution is stored on the transition to a Done-category status");
        bool conflictDetected = false;
        try {
            database.changeIssueStatus(created.key, "review", demoUserId, std::nullopt, created.version);
        } catch (const TicketHub::Domain::ConcurrencyConflict&) {
            conflictDetected = true;
        }
        require(conflictDetected, "stale status update is rejected");

        TicketHub::Domain::EditIssueRequest edit;
        edit.summary = "Verify portable database architecture (edited)";
        edit.description = "Updated by the SQLite integration test.";
        edit.priorityKey = "highest";
        edit.assigneeEmail = "sam@ticket-hub.local";
        edit.labels = {"database"};
        edit.storyPoints = 5.0;
        edit.dueDate = "2026-12-31";
        const auto edited = database.editIssue(created.key, edit, demoUserId, done->version);
        require(edited.has_value(), "edit succeeds");
        require(edited->summary == edit.summary, "summary is updated");
        require(edited->description == edit.description, "description is updated");
        require(edited->priority.key == "highest", "priority is updated");
        require(edited->assignee.has_value() && edited->assignee->email == "sam@ticket-hub.local",
               "assignee is updated");
        require(edited->storyPoints.has_value() && *edited->storyPoints == 5.0, "story points are updated");
        require(edited->dueDate.has_value() && *edited->dueDate == "2026-12-31", "due date is updated");
        require(edited->labels.size() == 1 && edited->labels[0] == "database", "labels are fully replaced");
        require(edited->version == done->version + 1, "edit increments the optimistic-lock version");
        require(edited->resolution.has_value() && *edited->resolution == "fixed",
               "editing standard fields does not disturb the resolution set by the earlier status change");

        bool editConflictDetected = false;
        try {
            database.editIssue(created.key, edit, demoUserId, done->version);
        } catch (const TicketHub::Domain::ConcurrencyConflict&) {
            editConflictDetected = true;
        }
        require(editConflictDetected, "stale edit is rejected");

        TicketHub::Domain::EditIssueRequest unassign = edit;
        unassign.assigneeEmail = std::nullopt;
        const auto unassigned = database.editIssue(created.key, unassign, demoUserId, edited->version);
        require(unassigned.has_value() && !unassigned->assignee.has_value(), "assignee can be cleared");

        const auto editHistoryCount = scalarInt(databasePath,
            "SELECT COUNT(*) FROM issue_history WHERE issue_id = '" + created.id
            + "' AND field_name IN ('summary','description','priority','assignee','story_points','due_date')");
        require(editHistoryCount >= 6, "each changed field writes an issue_history row");

        const auto link = database.createIssueLink(created.key, "TH-1", TicketHub::Domain::LinkTypeBlocks);
        require(!link.id.empty() && link.outward && link.otherIssueKey == "TH-1",
               "a link is created from the source issue's perspective");

        const auto sourceLinks = database.listIssueLinks(created.key);
        require(sourceLinks.size() == 1 && sourceLinks[0].outward && sourceLinks[0].otherIssueKey == "TH-1",
               "the source issue sees the link as outward");

        const auto targetLinks = database.listIssueLinks("TH-1");
        require(targetLinks.size() == 1 && !targetLinks[0].outward && targetLinks[0].otherIssueKey == created.key,
               "the target issue sees the same link as inward");

        bool duplicateLinkRejected = false;
        try {
            database.createIssueLink(created.key, "TH-1", TicketHub::Domain::LinkTypeBlocks);
        } catch (const std::invalid_argument&) {
            duplicateLinkRejected = true;
        }
        require(duplicateLinkRejected, "an exact-duplicate link is rejected");

        bool selfLinkRejected = false;
        try {
            database.createIssueLink(created.key, created.key, TicketHub::Domain::LinkTypeRelatesTo);
        } catch (const std::exception&) {
            selfLinkRejected = true;
        }
        require(selfLinkRejected, "a self-link is rejected by the database CHECK constraint");

        const auto linkDetail = database.findIssueLinkById(link.id);
        require(linkDetail.has_value() && linkDetail->sourceIssueKey == created.key
                    && linkDetail->targetIssueKey == "TH-1",
               "findIssueLinkById resolves both ends of the link");

        require(database.deleteIssueLink(link.id), "the link can be deleted");
        require(database.listIssueLinks(created.key).empty(), "the link no longer appears after deletion");
        require(!database.deleteIssueLink(link.id), "deleting an already-gone link returns false");

        const std::string alexUserId = "00000000-0000-4000-8000-000000000002";
        require(database.watchIssue(created.key, demoUserId), "watching an issue succeeds");
        require(!database.watchIssue(created.key, demoUserId), "watching an already-watched issue is a no-op");
        require(database.watchIssue(created.key, alexUserId), "a second user can also watch");
        require(database.listWatchers(created.key).size() == 2, "both watchers are listed");
        require(database.unwatchIssue(created.key, demoUserId), "unwatching removes the watcher");
        require(!database.unwatchIssue(created.key, demoUserId), "unwatching again is a no-op");
        require(database.listWatchers(created.key).size() == 1, "one watcher remains");

        require(database.voteIssue(created.key, demoUserId), "voting for an issue succeeds");
        require(!database.voteIssue(created.key, demoUserId), "voting again is a no-op");
        require(database.listVoters(created.key).size() == 1, "the voter is listed");
        require(database.unvoteIssue(created.key, demoUserId), "removing a vote succeeds");
        require(database.listVoters(created.key).empty(), "no voters remain");

        bool watchUnknownIssueRejected = false;
        try {
            database.watchIssue("TH-9999", demoUserId);
        } catch (const std::invalid_argument&) {
            watchUnknownIssueRejected = true;
        }
        require(watchUnknownIssueRejected, "watching an unknown issue is rejected");

        const auto comment = database.addComment({created.key, "Database adapter smoke test passed."}, demoUserId);
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
