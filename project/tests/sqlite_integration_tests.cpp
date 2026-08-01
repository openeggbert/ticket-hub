#include "infrastructure/database/SqliteDatabase.h"
#include "domain/Errors.h"

#include <algorithm>
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

        const auto containsIssue = [](const std::vector<TicketHub::Domain::Issue>& issues, const std::string& key) {
            return std::any_of(issues.begin(), issues.end(),
                               [&key](const auto& issue) { return issue.key == key; });
        };

        {
            IssueFilter typeFilter;
            typeFilter.projectKey = "TH";
            typeFilter.issueTypeKey = "task";
            require(containsIssue(database.listIssues(typeFilter), created.key), "issueTypeKey filter matches a task-type issue");
            typeFilter.issueTypeKey = "bug";
            require(!containsIssue(database.listIssues(typeFilter), created.key), "issueTypeKey filter excludes a non-matching type");

            IssueFilter priorityFilter;
            priorityFilter.priorityKey = "highest";
            require(containsIssue(database.listIssues(priorityFilter), created.key), "priorityKey filter matches the edited highest-priority issue");
            priorityFilter.priorityKey = "low";
            require(!containsIssue(database.listIssues(priorityFilter), created.key), "priorityKey filter excludes a non-matching priority");

            IssueFilter assigneeFilter;
            assigneeFilter.assigneeEmail = "sam@ticket-hub.local";
            require(containsIssue(database.listIssues(assigneeFilter), created.key), "assigneeEmail filter matches the edited assignee");
            assigneeFilter.assigneeEmail = "alex@ticket-hub.local";
            require(!containsIssue(database.listIssues(assigneeFilter), created.key), "assigneeEmail filter excludes a non-matching assignee");

            IssueFilter labelFilter;
            labelFilter.label = "database";
            require(containsIssue(database.listIssues(labelFilter), created.key), "label filter matches an issue carrying that label");
            require(database.listIssues(labelFilter).size() == 1,
                   "label filter does not corrupt the aggregated label list into a per-row match count");
            labelFilter.label = "backend";
            require(!containsIssue(database.listIssues(labelFilter), created.key), "label filter excludes an issue without that label");

            IssueFilter dueFilter;
            dueFilter.dueBefore = "2026-12-31";
            require(containsIssue(database.listIssues(dueFilter), created.key), "dueBefore filter is inclusive of the exact due date");
            dueFilter.dueBefore = "2026-12-30";
            require(!containsIssue(database.listIssues(dueFilter), created.key), "dueBefore filter excludes an issue due after the cutoff");

            IssueFilter descriptionSearch;
            descriptionSearch.search = "SQLite integration test";
            require(containsIssue(database.listIssues(descriptionSearch), created.key),
                   "search now matches the issue description, not just summary/key");

            IssueFilter combined;
            combined.projectKey = "TH";
            combined.issueTypeKey = "task";
            combined.priorityKey = "highest";
            combined.assigneeEmail = "sam@ticket-hub.local";
            combined.label = "database";
            require(containsIssue(database.listIssues(combined), created.key), "combined filters all narrow to the same edited issue");
            combined.label = "frontend";
            require(!containsIssue(database.listIssues(combined), created.key), "combined filters exclude when any single field mismatches");

            const auto stillHasBothLabels = database.findIssueByKey(created.key);
            require(stillHasBothLabels.has_value() && stillHasBothLabels->labels.size() == 1
                        && stillHasBothLabels->labels[0] == "database",
                   "filtering by label does not mutate the issue's own label list");
        }

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

        const auto alexWatched = database.listWatchedIssues(alexUserId, 10);
        require(alexWatched.size() == 1 && alexWatched[0].key == created.key,
               "listWatchedIssues (the reverse of listWatchers) returns the issues a given user is watching");
        require(database.listWatchedIssues(demoUserId, 10).empty(),
               "listWatchedIssues is empty for a user watching nothing (demo unwatched above)");

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
        require(comment.version == 1, "a new comment starts at version 1");
        require(!comment.editedAt.has_value(), "a new comment has no edited_at");
        require(database.listComments(created.key).size() == 1, "comment can be listed");

        // --- Comment editing and tombstone delete (D81/D82) ---
        const auto foundComment = database.findCommentById(comment.id);
        require(foundComment.has_value() && foundComment->body == comment.body, "findCommentById resolves the comment");

        const auto editedComment = database.editComment(comment.id, "Edited via the SQLite integration test.", demoUserId, comment.version);
        require(editedComment.has_value(), "editComment succeeds");
        require(editedComment->body == "Edited via the SQLite integration test.", "the body is updated");
        require(editedComment->version == comment.version + 1, "editing increments the optimistic-lock version");
        require(editedComment->editedAt.has_value(), "editing sets edited_at");

        bool commentEditConflictDetected = false;
        try {
            database.editComment(comment.id, "Stale edit.", demoUserId, comment.version);
        } catch (const TicketHub::Domain::ConcurrencyConflict&) {
            commentEditConflictDetected = true;
        }
        require(commentEditConflictDetected, "a stale comment edit is rejected");

        require(!database.editComment("00000000-0000-4000-8000-00000000dead", "n/a", demoUserId, std::nullopt).has_value(),
               "editing an unknown comment returns nullopt");

        // --- Fixed emoji reactions on comments (D84) ---
        require(database.addCommentReaction(comment.id, demoUserId, "thumbs_up"), "reacting to a comment succeeds");
        require(!database.addCommentReaction(comment.id, demoUserId, "thumbs_up"),
               "reacting twice with the same key is a no-op");
        require(database.addCommentReaction(comment.id, alexUserId, "thumbs_up"),
               "a second user can react with the same key");
        require(database.addCommentReaction(comment.id, demoUserId, "heart"),
               "the same user can react with a different key");
        require(database.listCommentReactions(comment.id).size() == 3, "all three reactions are listed");
        require(database.removeCommentReaction(comment.id, demoUserId, "heart"), "removing a reaction succeeds");
        require(!database.removeCommentReaction(comment.id, demoUserId, "heart"),
               "removing an already-removed reaction is a no-op");
        require(database.listCommentReactions(comment.id).size() == 2, "two reactions remain");

        // --- @mention handles and the fixed notification set (D56/D80/D14) ---
        const auto demoUser = database.findUserByHandle("demo");
        require(demoUser.has_value() && demoUser->id == demoUserId,
               "findUserByHandle resolves the seeded demo handle");
        require(!database.findUserByHandle("no-such-handle").has_value(),
               "findUserByHandle returns nullopt for an unknown handle");

        const auto assignedNotification = database.createNotification(alexUserId, "assigned", created.id);
        require(assignedNotification.type == "assigned", "createNotification returns the type it was given");
        require(assignedNotification.issueKey.has_value() && *assignedNotification.issueKey == created.key,
               "createNotification resolves the issue key via the stored issue_id");
        require(!assignedNotification.readAt.has_value(), "a new notification starts unread");

        database.createNotification(alexUserId, "mentioned", created.id);
        require(database.listNotifications(alexUserId, false).size() == 2, "both notifications are listed");
        require(database.countUnreadNotifications(alexUserId) == 2, "both notifications are unread");

        require(database.markNotificationRead(assignedNotification.id, alexUserId),
               "marking a notification read succeeds");
        require(!database.markNotificationRead(assignedNotification.id, alexUserId),
               "marking an already-read notification read again is a no-op");
        require(database.countUnreadNotifications(alexUserId) == 1, "one notification remains unread");
        require(database.listNotifications(alexUserId, true).size() == 1, "unreadOnly filters to the remaining one");
        require(!database.markNotificationRead(assignedNotification.id, demoUserId),
               "a different user cannot mark someone else's notification read");

        require(database.markAllNotificationsRead(alexUserId), "markAllNotificationsRead succeeds when unread remain");
        require(database.countUnreadNotifications(alexUserId) == 0, "no notifications remain unread");
        require(!database.markAllNotificationsRead(alexUserId), "marking all read again is a no-op");

        require(database.deleteComment(comment.id, demoUserId), "a comment can be soft-deleted");
        require(database.listComments(created.key).empty(), "a soft-deleted comment no longer appears in the list");
        require(!database.findCommentById(comment.id).has_value(), "a soft-deleted comment is not found by findCommentById");
        require(!database.deleteComment(comment.id, demoUserId), "deleting an already-deleted comment is a no-op");
        require(scalarInt(databasePath, "SELECT COUNT(*) FROM comments WHERE id = '" + comment.id + "'") == 1,
               "the soft-deleted comment's row (and original body) still physically exists");

        // --- Simplified worklogs (D12/D13) ---
        const auto worklog = database.addWorklog({created.key, "2026-07-30", 3600, std::string("Initial investigation")}, demoUserId);
        require(!worklog.id.empty(), "a worklog receives an id");
        require(worklog.workDate == "2026-07-30", "the work date round-trips");
        require(worklog.timeSpentSeconds == 3600, "the time spent round-trips");
        require(worklog.comment.has_value() && *worklog.comment == "Initial investigation", "the comment round-trips");
        require(worklog.version == 1, "a new worklog starts at version 1");

        database.addWorklog({created.key, "2026-07-31", 1800, std::nullopt}, alexUserId);
        require(database.listWorklogs(created.key).size() == 2, "both worklogs are listed");

        const auto foundWorklog = database.findWorklogById(worklog.id);
        require(foundWorklog.has_value() && foundWorklog->id == worklog.id, "findWorklogById resolves the worklog");

        const auto editedWorklog = database.editWorklog(worklog.id, {"2026-07-30", 7200, std::string("Updated estimate")}, worklog.version);
        require(editedWorklog.has_value(), "editWorklog succeeds");
        require(editedWorklog->timeSpentSeconds == 7200, "the time spent is updated");
        require(editedWorklog->comment.has_value() && *editedWorklog->comment == "Updated estimate", "the comment is updated");
        require(editedWorklog->version == worklog.version + 1, "editing increments the optimistic-lock version");

        bool worklogEditConflictDetected = false;
        try {
            database.editWorklog(worklog.id, {"2026-07-30", 100, std::nullopt}, worklog.version);
        } catch (const TicketHub::Domain::ConcurrencyConflict&) {
            worklogEditConflictDetected = true;
        }
        require(worklogEditConflictDetected, "a stale worklog edit is rejected");

        require(!database.editWorklog("00000000-0000-4000-8000-00000000dead", {"2026-07-30", 100, std::nullopt}, std::nullopt).has_value(),
               "editing an unknown worklog returns nullopt");

        require(database.deleteWorklog(worklog.id, demoUserId), "a worklog can be soft-deleted");
        require(database.listWorklogs(created.key).size() == 1, "a soft-deleted worklog no longer appears in the list");
        require(!database.findWorklogById(worklog.id).has_value(), "a soft-deleted worklog is not found by findWorklogById");
        require(!database.deleteWorklog(worklog.id, demoUserId), "deleting an already-deleted worklog is a no-op");
        require(scalarInt(databasePath, "SELECT COUNT(*) FROM worklogs WHERE id = '" + worklog.id + "'") == 1,
               "the soft-deleted worklog's row still physically exists");

        // --- Simple append-only admin/security audit log (D23) ---
        database.recordAuditEvent("admin", "project.permanently_deleted", demoUserId, "project", "TH", std::nullopt);
        database.recordAuditEvent("auth", "login.failed", std::nullopt, "user", alexUserId, std::nullopt);
        const auto auditEvents = database.listAuditEvents(10);
        require(auditEvents.size() == 2, "both recorded events are listed");
        require(auditEvents.front().category == "auth" && auditEvents.front().action == "login.failed",
               "listAuditEvents is newest-first");
        require(!auditEvents.front().actor.has_value(),
               "an event recorded with no actor (e.g. an unauthenticated login attempt) has no actor in the result");
        const auto& projectDeletedEvent = auditEvents.back();
        require(projectDeletedEvent.actor.has_value() && projectDeletedEvent.actor->id == demoUserId,
               "an event recorded with an actor resolves the actor's UserSummary");
        require(projectDeletedEvent.targetType.has_value() && *projectDeletedEvent.targetType == "project" &&
                    projectDeletedEvent.targetId.has_value() && *projectDeletedEvent.targetId == "TH",
               "target type/id round-trip");
        require(database.listAuditEvents(1).size() == 1, "the limit parameter caps the result size");

        const auto dashboard = database.dashboardStats();
        require(dashboard.totalIssues == 9, "dashboard includes newly created issue");
        require(!dashboard.recentIssues.empty() && dashboard.recentIssues.front().key == created.key,
                "dashboard returns the most recently updated issue first");

        // --- Manual ordering (D31) ---
        const auto th1Before = database.findIssueByKey("TH-1");
        const auto th3Before = database.findIssueByKey("TH-3");
        require(th1Before.has_value() && th3Before.has_value(), "TH-1 and TH-3 exist for the reorder test");
        require(th1Before->rankOrder < th3Before->rankOrder, "seeded issues start ranked in creation order");

        const auto reordered = database.reorderIssue("TH-3", std::string("TH-1"));
        require(reordered.key == "TH-3", "reorderIssue returns the moved issue");
        const auto th1AfterReorder = database.findIssueByKey("TH-1");
        const auto th3AfterReorder = database.findIssueByKey("TH-3");
        require(th3AfterReorder->rankOrder < th1AfterReorder->rankOrder,
               "TH-3 now ranks immediately before TH-1 after being reordered there");

        database.reorderIssue("TH-3", std::nullopt);
        const auto th3AfterEnd = database.findIssueByKey("TH-3");
        const auto th7AfterEnd = database.findIssueByKey(created.key);
        require(th3AfterEnd->rankOrder > th7AfterEnd->rankOrder,
               "reordering with no anchor appends the issue to the end of its project");

        bool reorderCrossProjectRejected = false;
        try {
            database.reorderIssue("TH-1", std::string("WEB-1"));
        } catch (const std::invalid_argument&) {
            reorderCrossProjectRejected = true;
        }
        require(reorderCrossProjectRejected, "reordering relative to an issue in a different project is rejected");

        bool reorderSelfRejected = false;
        try {
            database.reorderIssue("TH-1", std::string("TH-1"));
        } catch (const std::invalid_argument&) {
            reorderSelfRejected = true;
        }
        require(reorderSelfRejected, "reordering an issue before itself is rejected");

        // --- Move between projects (D37) ---
        const auto moved = database.moveIssue("TH-2", "WEB", demoUserId);
        require(moved.projectKey == "WEB", "the moved issue now belongs to the target project");
        require(moved.key == "WEB-3", "the moved issue receives the target project's next issue number");
        require(moved.rankOrder == 3, "the moved issue is appended after the target project's existing issues");

        const auto viaOldKey = database.findIssueByKey("TH-2");
        require(viaOldKey.has_value() && viaOldKey->key == "WEB-3",
               "the vacated source key permanently resolves to the moved issue via issue_key_aliases");
        require(scalarInt(databasePath, "SELECT COUNT(*) FROM issue_key_aliases WHERE alias_key = 'TH-2'") == 1,
               "the vacated key is recorded as a permanent alias");
        require(scalarInt(databasePath,
                    "SELECT COUNT(*) FROM issue_history WHERE issue_id = '" + moved.id + "' AND field_name = 'project'") == 1,
               "the move writes an issue_history row for the project field");

        bool moveSameProjectRejected = false;
        try {
            database.moveIssue("TH-1", "TH", demoUserId);
        } catch (const std::invalid_argument&) {
            moveSameProjectRejected = true;
        }
        require(moveSameProjectRejected, "moving an issue to its own project is rejected");

        bool moveUnknownProjectRejected = false;
        try {
            database.moveIssue("TH-1", "NOPE", demoUserId);
        } catch (const std::invalid_argument&) {
            moveUnknownProjectRejected = true;
        }
        require(moveUnknownProjectRejected, "moving an issue to an unknown project is rejected");

        CreateIssueRequest parentForMove;
        parentForMove.projectKey = "TH";
        parentForMove.summary = "Parent for move-rejection test";
        parentForMove.issueTypeKey = "story";
        parentForMove.priorityKey = "medium";
        const auto moveParent = database.createIssue(parentForMove, demoUserId);

        CreateIssueRequest childForMove;
        childForMove.projectKey = "TH";
        childForMove.summary = "Child for move-rejection test";
        childForMove.issueTypeKey = "sub-task";
        childForMove.priorityKey = "medium";
        childForMove.parentIssueKey = moveParent.key;
        const auto moveChild = database.createIssue(childForMove, demoUserId);

        bool moveIssueWithChildrenRejected = false;
        try {
            database.moveIssue(moveParent.key, "WEB", demoUserId);
        } catch (const std::invalid_argument&) {
            moveIssueWithChildrenRejected = true;
        }
        require(moveIssueWithChildrenRejected, "moving an issue that has children is rejected");

        bool moveIssueWithParentRejected = false;
        try {
            database.moveIssue(moveChild.key, "WEB", demoUserId);
        } catch (const std::invalid_argument&) {
            moveIssueWithParentRejected = true;
        }
        require(moveIssueWithParentRejected, "moving an issue that has a parent is rejected");

        require(database.softDeleteIssue(created.key, demoUserId), "an issue can be soft-deleted");
        require(!database.findIssueByKey(created.key).has_value(),
               "a soft-deleted issue is not found by ordinary lookup");
        require(!database.softDeleteIssue(created.key, demoUserId), "soft-deleting an already-deleted issue is a no-op");

        const auto deletedIssues = database.listDeletedIssues();
        require(deletedIssues.size() == 1 && deletedIssues[0].key == created.key,
               "the deleted issue appears in the recycle bin");

        require(database.restoreIssue(created.key), "the issue can be restored");
        require(database.findIssueByKey(created.key).has_value(),
               "a restored issue is found again by ordinary lookup");
        require(database.listDeletedIssues().empty(), "the recycle bin is empty again after restore");

        require(database.softDeleteIssue(created.key, demoUserId), "re-deleting for the permanent-delete test");
        require(database.permanentlyDeleteIssue(created.key), "the issue can be permanently deleted");
        require(!database.permanentlyDeleteIssue(created.key),
               "permanently deleting an already-gone issue returns false");
        require(scalarInt(databasePath, "SELECT COUNT(*) FROM comments WHERE issue_id = '" + created.id + "'") == 0,
               "comments cascade-delete with the permanently-deleted issue");
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
