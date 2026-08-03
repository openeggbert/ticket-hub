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
    using TicketHub::Domain::CreateTicketRequest;
    using TicketHub::Domain::TicketFilter;
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

        const auto initialTickets = database.listTickets(TicketFilter{});
        require(initialTickets.size() == 8, "demo seed creates eight tickets");

        CreateTicketRequest request;
        request.projectKey = "TH";
        request.summary = "Verify portable database architecture";
        request.description = "Created by the SQLite integration test.";
        request.ticketTypeKey = "task";
        request.priorityKey = "high";
        request.assigneeEmail = "alex@ticket-hub.local";
        request.labels = {"database", "integration"};
        request.storyPoints = 3.0;

        const auto created = database.createTicket(request, demoUserId);
        require(created.key == "TH-7", "project-local ticket counter creates TH-7");
        require(created.labels.size() == 2, "created ticket has both labels");

        const auto found = database.findTicketByKey(created.key);
        require(found.has_value() && found->summary == request.summary, "created ticket can be read back");

        executeSql(databasePath,
                   "INSERT INTO ticket_key_aliases(alias_key, ticket_id) VALUES ('LEGACY-7', '" + created.id + "')");
        const auto viaAlias = database.findTicketByKey("LEGACY-7");
        require(viaAlias.has_value() && viaAlias->key == created.key, "permanent ticket-key alias resolves to current key");

        require(database.changeTicketStatus(created.key, "done", demoUserId, std::string("fixed"), created.version),
               "ticket status can be changed");
        const auto done = database.findTicketByKey(created.key);
        require(done.has_value() && done->status.key == "done", "changed status is persisted");
        require(done->version == created.version + 1, "status change increments optimistic-lock version");
        require(done->resolution.has_value() && *done->resolution == "fixed",
               "resolution is stored on the transition to a Done-category status");
        bool conflictDetected = false;
        try {
            database.changeTicketStatus(created.key, "review", demoUserId, std::nullopt, created.version);
        } catch (const TicketHub::Domain::ConcurrencyConflict&) {
            conflictDetected = true;
        }
        require(conflictDetected, "stale status update is rejected");

        TicketHub::Domain::EditTicketRequest edit;
        edit.summary = "Verify portable database architecture (edited)";
        edit.description = "Updated by the SQLite integration test.";
        edit.priorityKey = "highest";
        edit.ticketTypeKey = "task";
        edit.assigneeEmail = "sam@ticket-hub.local";
        edit.labels = {"database"};
        edit.storyPoints = 5.0;
        edit.dueDate = "2026-12-31";
        const auto edited = database.editTicket(created.key, edit, demoUserId, done->version);
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

        const auto containsTicket = [](const std::vector<TicketHub::Domain::Ticket>& tickets, const std::string& key) {
            return std::any_of(tickets.begin(), tickets.end(),
                               [&key](const auto& ticket) { return ticket.key == key; });
        };

        {
            TicketFilter typeFilter;
            typeFilter.projectKey = "TH";
            typeFilter.ticketTypeKey = "task";
            require(containsTicket(database.listTickets(typeFilter), created.key), "ticketTypeKey filter matches a task-type ticket");
            typeFilter.ticketTypeKey = "bug";
            require(!containsTicket(database.listTickets(typeFilter), created.key), "ticketTypeKey filter excludes a non-matching type");

            TicketFilter priorityFilter;
            priorityFilter.priorityKey = "highest";
            require(containsTicket(database.listTickets(priorityFilter), created.key), "priorityKey filter matches the edited highest-priority ticket");
            priorityFilter.priorityKey = "low";
            require(!containsTicket(database.listTickets(priorityFilter), created.key), "priorityKey filter excludes a non-matching priority");

            TicketFilter assigneeFilter;
            assigneeFilter.assigneeEmail = "sam@ticket-hub.local";
            require(containsTicket(database.listTickets(assigneeFilter), created.key), "assigneeEmail filter matches the edited assignee");
            assigneeFilter.assigneeEmail = "alex@ticket-hub.local";
            require(!containsTicket(database.listTickets(assigneeFilter), created.key), "assigneeEmail filter excludes a non-matching assignee");

            TicketFilter labelFilter;
            labelFilter.label = "database";
            require(containsTicket(database.listTickets(labelFilter), created.key), "label filter matches a ticket carrying that label");
            require(database.listTickets(labelFilter).size() == 1,
                   "label filter does not corrupt the aggregated label list into a per-row match count");
            labelFilter.label = "backend";
            require(!containsTicket(database.listTickets(labelFilter), created.key), "label filter excludes a ticket without that label");

            TicketFilter dueFilter;
            dueFilter.dueBefore = "2026-12-31";
            require(containsTicket(database.listTickets(dueFilter), created.key), "dueBefore filter is inclusive of the exact due date");
            dueFilter.dueBefore = "2026-12-30";
            require(!containsTicket(database.listTickets(dueFilter), created.key), "dueBefore filter excludes a ticket due after the cutoff");

            TicketFilter descriptionSearch;
            descriptionSearch.search = "SQLite integration test";
            require(containsTicket(database.listTickets(descriptionSearch), created.key),
                   "search now matches the ticket description, not just summary/key");

            TicketFilter combined;
            combined.projectKey = "TH";
            combined.ticketTypeKey = "task";
            combined.priorityKey = "highest";
            combined.assigneeEmail = "sam@ticket-hub.local";
            combined.label = "database";
            require(containsTicket(database.listTickets(combined), created.key), "combined filters all narrow to the same edited ticket");
            combined.label = "frontend";
            require(!containsTicket(database.listTickets(combined), created.key), "combined filters exclude when any single field mismatches");

            // Numbered/offset pagination (D126).
            const auto allTickets = database.listTickets(TicketFilter{});
            require(database.countTickets(TicketFilter{}) == static_cast<std::int64_t>(allTickets.size()),
                   "countTickets matches the unpaginated listTickets row count for the same filter");

            const auto firstPage = database.listTickets(TicketFilter{}, 3, 0);
            require(firstPage.size() == 3, "paginated listTickets respects the limit");
            const auto secondPage = database.listTickets(TicketFilter{}, 3, 3);
            require(secondPage.size() == 3, "second page also respects the limit");
            require(firstPage[0].key != secondPage[0].key, "offset actually advances past the first page");
            for (const auto& firstPageTicket : firstPage) {
                require(!containsTicket(secondPage, firstPageTicket.key), "pages do not overlap");
            }

            const auto pastTheEnd = database.listTickets(TicketFilter{}, 100, 100);
            require(pastTheEnd.empty(), "an offset beyond the total row count returns an empty page, not an error");

            const auto wholeSetAsOnePage = database.listTickets(TicketFilter{}, 100, 0);
            require(wholeSetAsOnePage.size() == allTickets.size(),
                   "a limit exceeding the total row count returns every matching row, not an error");

            TicketFilter projectFilter;
            projectFilter.projectKey = "TH";
            const auto projectTicketsUnpaged = database.listTickets(projectFilter);
            require(database.countTickets(projectFilter) == static_cast<std::int64_t>(projectTicketsUnpaged.size()),
                   "countTickets respects the same filter as the paginated/unpaginated listTickets overloads");
            const auto projectFirstPage = database.listTickets(projectFilter, 2, 0);
            require(projectFirstPage.size() == 2, "pagination and filtering compose correctly");
            for (const auto& ticket : projectFirstPage) {
                require(ticket.projectKey == "TH", "a paginated+filtered page still only contains matching rows");
            }

            const auto stillHasBothLabels = database.findTicketByKey(created.key);
            require(stillHasBothLabels.has_value() && stillHasBothLabels->labels.size() == 1
                        && stillHasBothLabels->labels[0] == "database",
                   "filtering by label does not mutate the ticket's own label list");
        }

        bool editConflictDetected = false;
        try {
            database.editTicket(created.key, edit, demoUserId, done->version);
        } catch (const TicketHub::Domain::ConcurrencyConflict&) {
            editConflictDetected = true;
        }
        require(editConflictDetected, "stale edit is rejected");

        TicketHub::Domain::EditTicketRequest unassign = edit;
        unassign.assigneeEmail = std::nullopt;
        const auto unassigned = database.editTicket(created.key, unassign, demoUserId, edited->version);
        require(unassigned.has_value() && !unassigned->assignee.has_value(), "assignee can be cleared");

        const auto editHistoryCount = scalarInt(databasePath,
            "SELECT COUNT(*) FROM ticket_history WHERE ticket_id = '" + created.id
            + "' AND field_name IN ('summary','description','priority','assignee','story_points','due_date')");
        require(editHistoryCount >= 6, "each changed field writes a ticket_history row");

        // --- History tab (D129/D37's own field-change log, exposed for the
        // first time via listTicketHistory/GET .../history) ---
        {
            const auto history = database.listTicketHistory(created.key);
            require(history.size() >= 7,
                   "listTicketHistory returns the status-change row plus every changed-field row from editTicket");
            // Strict `>` (not `>=`): several of these rows share the exact
            // same created_at (multiple fields changed in one editTicket
            // transaction), and is_sorted's comparator must be a strict
            // weak ordering -- `>=` is reflexive (x >= x is true), which
            // makes is_sorted treat every tied adjacent pair as "out of
            // order" and fail even though the query's own `ORDER BY
            // created_at DESC` is honored correctly.
            require(std::is_sorted(history.begin(), history.end(),
                                   [](const auto& a, const auto& b) { return a.createdAt > b.createdAt; }),
                   "history is ordered newest first (non-increasing created_at)");

            const auto statusEntry = std::find_if(history.begin(), history.end(),
                                                  [](const auto& entry) { return entry.fieldName == "status"; });
            require(statusEntry != history.end(), "the status transition is recorded");
            require(statusEntry->oldValue.has_value() && *statusEntry->oldValue == "backlog",
                   "the status entry records the old value (a new ticket starts in backlog)");
            require(statusEntry->newValue.has_value() && *statusEntry->newValue == "done",
                   "the status entry records the new value");
            require(statusEntry->actor.has_value() && statusEntry->actor->email == "demo@ticket-hub.local",
                   "the actor who made the change is resolved from the nullable actor_user_id FK");

            const auto assigneeEntry = std::find_if(history.begin(), history.end(),
                                                    [](const auto& entry) { return entry.fieldName == "assignee"; });
            require(assigneeEntry != history.end() && assigneeEntry->newValue.has_value(),
                   "the assignee field change from editTicket is recorded");

            const auto unknownTicketHistory = database.listTicketHistory("TH-9999");
            require(unknownTicketHistory.empty(), "an unknown ticket key returns an empty history, not an error");
        }

        // --- Project components (D19, KEEP_FOR_V1) --- uses a freshly created
        // ticket rather than `created`/`edited`, so it does not disturb the
        // optimistic-lock version chain the stale-edit tests above depend on.
        {
            TicketHub::Domain::CreateComponentRequest componentRequest;
            componentRequest.projectKey = "TH";
            componentRequest.name = "Backend";
            componentRequest.description = "Server-side work";
            componentRequest.leadEmail = "alex@ticket-hub.local";
            const auto component = database.createComponent(componentRequest);
            require(!component.id.empty(), "component is created with an id");
            require(component.name == "Backend", "component name is stored");
            require(component.lead.has_value() && component.lead->email == "alex@ticket-hub.local",
                   "component lead is resolved");

            bool duplicateRejected = false;
            try {
                database.createComponent(componentRequest);
            } catch (const std::invalid_argument&) {
                duplicateRejected = true;
            }
            require(duplicateRejected, "a duplicate component name in the same project is rejected");

            const auto components = database.listComponents("TH");
            require(components.size() == 1, "listComponents returns the newly created component");

            CreateTicketRequest componentTicketRequest;
            componentTicketRequest.projectKey = "TH";
            componentTicketRequest.summary = "Ticket carrying a component";
            componentTicketRequest.componentName = "Backend";
            const auto componentTicket = database.createTicket(componentTicketRequest, demoUserId);
            require(componentTicket.component.has_value() && componentTicket.component->name == "Backend",
                   "a ticket created with componentName is linked to that component");

            TicketFilter componentFilter;
            componentFilter.componentName = "Backend";
            require(containsTicket(database.listTickets(componentFilter), componentTicket.key),
                   "componentName filter matches a ticket carrying that component");
            require(database.countTickets(componentFilter)
                        == static_cast<std::int64_t>(database.listTickets(componentFilter).size()),
                   "countTickets agrees with listTickets for the componentName filter");
            componentFilter.componentName = "Frontend";
            require(!containsTicket(database.listTickets(componentFilter), componentTicket.key),
                   "componentName filter excludes a ticket without a matching component");

            const auto editedComponent = database.editComponent(component.id,
                TicketHub::Domain::EditComponentRequest{"Backend Services", "Renamed", std::nullopt,
                                                         std::string("sam@ticket-hub.local")});
            require(editedComponent.has_value() && editedComponent->name == "Backend Services",
                   "editComponent renames the component");
            require(!editedComponent->lead.has_value(), "editComponent clears the lead when leadEmail is nullopt");
            require(editedComponent->defaultAssignee.has_value()
                        && editedComponent->defaultAssignee->email == "sam@ticket-hub.local",
                   "editComponent sets the default assignee");

            require(!database.editComponent("unknown-component-id",
                        TicketHub::Domain::EditComponentRequest{"X", "", std::nullopt, std::nullopt}).has_value(),
                   "editing an unknown component returns nullopt");
            require(!database.findComponentById("unknown-component-id").has_value(),
                   "finding an unknown component returns nullopt");

            require(database.deleteComponent(component.id), "deleteComponent removes the component");
            require(!database.deleteComponent(component.id), "deleting an already-deleted component returns false");

            const auto afterDelete = database.findTicketByKey(componentTicket.key);
            require(afterDelete.has_value() && !afterDelete->component.has_value(),
                   "deleting a component clears it from any ticket that referenced it (ON DELETE SET NULL)");

            // Fully remove this block's own scratch ticket so it does not
            // throw off the dashboard/count/recycle-bin assertions later in
            // this file, which assert exact totals against the fixed seed +
            // `created` (a merely soft-deleted ticket would still show up
            // in the recycle-bin size assertion below).
            require(database.softDeleteTicket(componentTicket.key, demoUserId),
                   "cleanup: the component-test scratch ticket can be soft-deleted");
            require(database.permanentlyDeleteTicket(componentTicket.key),
                   "cleanup: the component-test scratch ticket can be permanently deleted");
        }

        const auto link = database.createTicketLink(created.key, "TH-1", TicketHub::Domain::LinkTypeBlocks);
        require(!link.id.empty() && link.outward && link.otherTicketKey == "TH-1",
               "a link is created from the source ticket's perspective");

        const auto sourceLinks = database.listTicketLinks(created.key);
        require(sourceLinks.size() == 1 && sourceLinks[0].outward && sourceLinks[0].otherTicketKey == "TH-1",
               "the source ticket sees the link as outward");

        const auto targetLinks = database.listTicketLinks("TH-1");
        require(targetLinks.size() == 1 && !targetLinks[0].outward && targetLinks[0].otherTicketKey == created.key,
               "the target ticket sees the same link as inward");

        bool duplicateLinkRejected = false;
        try {
            database.createTicketLink(created.key, "TH-1", TicketHub::Domain::LinkTypeBlocks);
        } catch (const std::invalid_argument&) {
            duplicateLinkRejected = true;
        }
        require(duplicateLinkRejected, "an exact-duplicate link is rejected");

        bool selfLinkRejected = false;
        try {
            database.createTicketLink(created.key, created.key, TicketHub::Domain::LinkTypeRelatesTo);
        } catch (const std::exception&) {
            selfLinkRejected = true;
        }
        require(selfLinkRejected, "a self-link is rejected by the database CHECK constraint");

        const auto linkDetail = database.findTicketLinkById(link.id);
        require(linkDetail.has_value() && linkDetail->sourceTicketKey == created.key
                    && linkDetail->targetTicketKey == "TH-1",
               "findTicketLinkById resolves both ends of the link");

        require(database.deleteTicketLink(link.id), "the link can be deleted");
        require(database.listTicketLinks(created.key).empty(), "the link no longer appears after deletion");
        require(!database.deleteTicketLink(link.id), "deleting an already-gone link returns false");

        const std::string alexUserId = "00000000-0000-4000-8000-000000000002";
        require(database.watchTicket(created.key, demoUserId), "watching a ticket succeeds");
        require(!database.watchTicket(created.key, demoUserId), "watching an already-watched ticket is a no-op");
        require(database.watchTicket(created.key, alexUserId), "a second user can also watch");
        require(database.listWatchers(created.key).size() == 2, "both watchers are listed");
        require(database.unwatchTicket(created.key, demoUserId), "unwatching removes the watcher");
        require(!database.unwatchTicket(created.key, demoUserId), "unwatching again is a no-op");
        require(database.listWatchers(created.key).size() == 1, "one watcher remains");

        const auto alexWatched = database.listWatchedTickets(alexUserId, 10);
        require(alexWatched.size() == 1 && alexWatched[0].key == created.key,
               "listWatchedTickets (the reverse of listWatchers) returns the tickets a given user is watching");
        require(database.listWatchedTickets(demoUserId, 10).empty(),
               "listWatchedTickets is empty for a user watching nothing (demo unwatched above)");

        require(database.voteTicket(created.key, demoUserId), "voting for a ticket succeeds");
        require(!database.voteTicket(created.key, demoUserId), "voting again is a no-op");
        require(database.listVoters(created.key).size() == 1, "the voter is listed");
        require(database.unvoteTicket(created.key, demoUserId), "removing a vote succeeds");
        require(database.listVoters(created.key).empty(), "no voters remain");

        bool watchUnknownTicketRejected = false;
        try {
            database.watchTicket("TH-9999", demoUserId);
        } catch (const std::invalid_argument&) {
            watchUnknownTicketRejected = true;
        }
        require(watchUnknownTicketRejected, "watching an unknown ticket is rejected");

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
        require(assignedNotification.ticketKey.has_value() && *assignedNotification.ticketKey == created.key,
               "createNotification resolves the ticket key via the stored ticket_id");
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

        // --- Attachments (Phase 5, D15/D98-D105) ---
        {
            const auto attachment = database.createAttachment("60000000-0000-4000-8000-0000000000a1", created.key,
                demoUserId, "notes.txt", "text/plain", 11, "deadbeef");
            require(attachment.id == "60000000-0000-4000-8000-0000000000a1", "createAttachment uses the caller-supplied id");
            require(attachment.fileName == "notes.txt" && attachment.contentType == "text/plain",
                   "file name and content type round-trip");
            require(attachment.byteSize == 11, "byte size round-trips");
            require(attachment.sha256 == "deadbeef", "sha256 round-trips");
            require(!attachment.deletedAt.has_value(), "a freshly created attachment has no deletedAt");

            require(database.listAttachments(created.key).size() == 1, "the attachment appears in the ticket's list");

            const auto found = database.findAttachmentById(attachment.id);
            require(found.has_value() && found->fileName == "notes.txt", "findAttachmentById resolves the attachment");
            require(!database.findAttachmentById("00000000-0000-4000-8000-00000000dead").has_value(),
                   "findAttachmentById returns nullopt for an unknown id");

            require(database.softDeleteAttachment(attachment.id, demoUserId), "an attachment can be soft-deleted");
            require(database.listAttachments(created.key).empty(),
                   "a soft-deleted attachment no longer appears in the ticket's list");
            require(!database.softDeleteAttachment(attachment.id, demoUserId), "soft-deleting again is a no-op");

            const auto deleted = database.listDeletedAttachments();
            require(deleted.size() == 1 && deleted[0].id == attachment.id, "the soft-deleted attachment appears in the recycle bin");
            require(deleted[0].deletedAt.has_value(), "a recycle-bin attachment reports its deletedAt");

            require(database.restoreAttachment(attachment.id), "the attachment can be restored");
            require(database.listAttachments(created.key).size() == 1, "a restored attachment reappears in the ticket's list");
            require(!database.restoreAttachment(attachment.id), "restoring an already-active attachment is a no-op");

            require(database.softDeleteAttachment(attachment.id, demoUserId), "re-deleted for the permanent-delete test");
            require(database.permanentlyDeleteAttachment(attachment.id), "a soft-deleted attachment can be permanently deleted");
            require(!database.permanentlyDeleteAttachment(attachment.id),
                   "permanently deleting an already-gone attachment returns false");
            require(!database.findAttachmentById(attachment.id).has_value(),
                   "a permanently deleted attachment is truly gone, unlike soft delete");

            const auto secondAttachment = database.createAttachment("60000000-0000-4000-8000-0000000000a2", created.key,
                demoUserId, "diagram.png", "image/png", 2048, "cafef00d");
            const auto keysForTicket = database.listAttachmentStorageKeysForTicket(created.key);
            require(keysForTicket.size() == 1 && keysForTicket[0] == secondAttachment.id,
                   "listAttachmentStorageKeysForTicket returns every attachment's storage key regardless of soft-delete state");

            const auto keysForProject = database.listAttachmentStorageKeysForProject("TH");
            require(std::find(keysForProject.begin(), keysForProject.end(), secondAttachment.id) != keysForProject.end(),
                   "listAttachmentStorageKeysForProject includes attachments from every ticket in the project");

            bool unknownTicketRejected = false;
            try {
                database.createAttachment("60000000-0000-4000-8000-0000000000a3", "TH-9999", demoUserId, "x.txt", "text/plain", 1, "aa");
            } catch (const std::invalid_argument&) {
                unknownTicketRejected = true;
            }
            require(unknownTicketRejected, "creating an attachment on an unknown ticket is rejected");
        }

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
        require(dashboard.totalTickets == 9, "dashboard includes newly created ticket");
        require(!dashboard.recentTickets.empty() && dashboard.recentTickets.front().key == created.key,
                "dashboard returns the most recently updated ticket first");

        // --- Kanban board WIP limits (D32/D33) ---
        {
            const auto columns = database.listBoardColumns();
            require(columns.size() == 5, "one board column per fixed workflow status");
            require(std::is_sorted(columns.begin(), columns.end(),
                                   [](const auto& a, const auto& b) { return a.sortOrder < b.sortOrder; }),
                   "board columns are ordered by sort_order");
            const auto inProgress = std::find_if(columns.begin(), columns.end(),
                                                 [](const auto& c) { return c.statusKey == "in-progress"; });
            require(inProgress != columns.end() && inProgress->wipLimit.has_value() && *inProgress->wipLimit == 3,
                   "the seeded In Progress column has the demo WIP limit of 3");
            const auto backlog = std::find_if(columns.begin(), columns.end(),
                                              [](const auto& c) { return c.statusKey == "backlog"; });
            require(backlog != columns.end() && !backlog->wipLimit.has_value(),
                   "an unlimited column reports no WIP limit");

            require(database.setBoardColumnWipLimit("backlog", 5), "setting a WIP limit succeeds for a known status");
            const auto afterSet = database.listBoardColumns();
            const auto backlogAfter = std::find_if(afterSet.begin(), afterSet.end(),
                                                    [](const auto& c) { return c.statusKey == "backlog"; });
            require(backlogAfter != afterSet.end() && backlogAfter->wipLimit.has_value() && *backlogAfter->wipLimit == 5,
                   "the new WIP limit is persisted");

            require(database.setBoardColumnWipLimit("backlog", std::nullopt), "a limit can be cleared back to unlimited");
            const auto afterClear = database.listBoardColumns();
            const auto backlogCleared = std::find_if(afterClear.begin(), afterClear.end(),
                                                      [](const auto& c) { return c.statusKey == "backlog"; });
            require(backlogCleared != afterClear.end() && !backlogCleared->wipLimit.has_value(),
                   "clearing a WIP limit removes it");

            require(!database.setBoardColumnWipLimit("not-a-real-status", 1),
                   "setting a WIP limit on an unknown status key returns false");
        }

        // --- Manual ordering (D31) ---
        const auto th1Before = database.findTicketByKey("TH-1");
        const auto th3Before = database.findTicketByKey("TH-3");
        require(th1Before.has_value() && th3Before.has_value(), "TH-1 and TH-3 exist for the reorder test");
        require(th1Before->rankOrder < th3Before->rankOrder, "seeded tickets start ranked in creation order");

        const auto reordered = database.reorderTicket("TH-3", std::string("TH-1"));
        require(reordered.key == "TH-3", "reorderTicket returns the moved ticket");
        const auto th1AfterReorder = database.findTicketByKey("TH-1");
        const auto th3AfterReorder = database.findTicketByKey("TH-3");
        require(th3AfterReorder->rankOrder < th1AfterReorder->rankOrder,
               "TH-3 now ranks immediately before TH-1 after being reordered there");

        database.reorderTicket("TH-3", std::nullopt);
        const auto th3AfterEnd = database.findTicketByKey("TH-3");
        const auto th7AfterEnd = database.findTicketByKey(created.key);
        require(th3AfterEnd->rankOrder > th7AfterEnd->rankOrder,
               "reordering with no anchor appends the ticket to the end of its project");

        // --- Backlog screen ordering (`sort=rank`): listTickets sorts by
        // rank_order/ticket_number instead of the default updated_at DESC
        // when TicketFilter::sortByRank is set, in both the unpaginated and
        // paginated overloads. ---
        {
            TicketFilter rankFilter;
            rankFilter.projectKey = "TH";
            rankFilter.sortByRank = true;
            const auto rankOrdered = database.listTickets(rankFilter);
            require(std::is_sorted(rankOrdered.begin(), rankOrdered.end(),
                                   [](const auto& a, const auto& b) { return a.rankOrder < b.rankOrder; }),
                   "sortByRank orders listTickets by rank_order instead of updated_at");
            require(!rankOrdered.empty() && rankOrdered.back().key == "TH-3",
                   "TH-3 (reordered to the end above) sorts last by rank");

            const auto rankOrderedPaged = database.listTickets(rankFilter, 200, 0);
            require(std::is_sorted(rankOrderedPaged.begin(), rankOrderedPaged.end(),
                                   [](const auto& a, const auto& b) { return a.rankOrder < b.rankOrder; }),
                   "sortByRank also orders the paginated listTickets overload by rank_order");

            TicketFilter defaultOrderFilter;
            defaultOrderFilter.projectKey = "TH";
            const auto defaultOrdered = database.listTickets(defaultOrderFilter);
            require(!std::is_sorted(defaultOrdered.begin(), defaultOrdered.end(),
                                    [](const auto& a, const auto& b) { return a.rankOrder < b.rankOrder; }),
                   "without sortByRank, listTickets does not happen to already be in rank order "
                   "(TH-3 was just reordered to the end above, so the default updated_at-based order "
                   "and rank order now disagree)");
        }

        // --- Retroactively confirming a resolution on a ticket that reached
        // Done without one (found while giving the ticket drawer a more
        // Jira-like layout: the resolution-confirm UI is reachable whenever
        // a ticket is Done-category with no resolution, regardless of
        // whether the requested status equals the current one -- but
        // changeTicketStatus's same-status guard used to always no-op,
        // silently dropping the resolution). Unreachable through the normal
        // API (which requires a resolution for any real transition into
        // Done), but can happen with historical/imported data. ---
        {
            CreateTicketRequest doneRequest;
            doneRequest.projectKey = "TH";
            doneRequest.summary = "Ticket for retroactive resolution test";
            const auto doneTicket = database.createTicket(doneRequest, demoUserId);
            executeSql(databasePath,
                       "UPDATE tickets SET status_id = (SELECT id FROM ticket_statuses WHERE status_key = 'done') "
                       "WHERE id = '" + doneTicket.id + "'");
            const auto beforeConfirm = database.findTicketByKey(doneTicket.key);
            require(beforeConfirm.has_value() && beforeConfirm->status.key == "done" && !beforeConfirm->resolution.has_value(),
                   "the ticket is now Done with no resolution, the state this fix targets");

            require(database.changeTicketStatus(doneTicket.key, "done", demoUserId, std::string("fixed"), beforeConfirm->version),
                   "a same-status call with a resolution succeeds instead of silently no-opping");
            const auto afterConfirm = database.findTicketByKey(doneTicket.key);
            require(afterConfirm.has_value() && afterConfirm->resolution.has_value() && *afterConfirm->resolution == "fixed",
                   "the resolution is actually persisted");
            require(afterConfirm->version == beforeConfirm->version + 1,
                   "the optimistic-lock version is incremented, same as any other status-endpoint write");

            require(database.changeTicketStatus(doneTicket.key, "done", demoUserId, std::string("wont-fix"), afterConfirm->version),
                   "a same-status call is still accepted (no-op) once a resolution already exists");
            const auto afterNoop = database.findTicketByKey(doneTicket.key);
            require(afterNoop.has_value() && afterNoop->resolution.has_value() && *afterNoop->resolution == "fixed",
                   "the existing resolution is NOT overwritten by a same-status no-op call");
            require(afterNoop->version == afterConfirm->version,
                   "a genuine no-op does not increment the version");
        }

        bool reorderCrossProjectRejected = false;
        try {
            database.reorderTicket("TH-1", std::string("WEB-1"));
        } catch (const std::invalid_argument&) {
            reorderCrossProjectRejected = true;
        }
        require(reorderCrossProjectRejected, "reordering relative to a ticket in a different project is rejected");

        bool reorderSelfRejected = false;
        try {
            database.reorderTicket("TH-1", std::string("TH-1"));
        } catch (const std::invalid_argument&) {
            reorderSelfRejected = true;
        }
        require(reorderSelfRejected, "reordering a ticket before itself is rejected");

        // --- Move between projects (D37) ---
        const auto moved = database.moveTicket("TH-2", "WEB", demoUserId);
        require(moved.projectKey == "WEB", "the moved ticket now belongs to the target project");
        require(moved.key == "WEB-3", "the moved ticket receives the target project's next ticket number");
        require(moved.rankOrder == 3, "the moved ticket is appended after the target project's existing tickets");

        const auto viaOldKey = database.findTicketByKey("TH-2");
        require(viaOldKey.has_value() && viaOldKey->key == "WEB-3",
               "the vacated source key permanently resolves to the moved ticket via ticket_key_aliases");
        require(scalarInt(databasePath, "SELECT COUNT(*) FROM ticket_key_aliases WHERE alias_key = 'TH-2'") == 1,
               "the vacated key is recorded as a permanent alias");
        require(scalarInt(databasePath,
                    "SELECT COUNT(*) FROM ticket_history WHERE ticket_id = '" + moved.id + "' AND field_name = 'project'") == 1,
               "the move writes a ticket_history row for the project field");

        bool moveSameProjectRejected = false;
        try {
            database.moveTicket("TH-1", "TH", demoUserId);
        } catch (const std::invalid_argument&) {
            moveSameProjectRejected = true;
        }
        require(moveSameProjectRejected, "moving a ticket to its own project is rejected");

        bool moveUnknownProjectRejected = false;
        try {
            database.moveTicket("TH-1", "NOPE", demoUserId);
        } catch (const std::invalid_argument&) {
            moveUnknownProjectRejected = true;
        }
        require(moveUnknownProjectRejected, "moving a ticket to an unknown project is rejected");

        CreateTicketRequest parentForMove;
        parentForMove.projectKey = "TH";
        parentForMove.summary = "Parent for move-rejection test";
        parentForMove.ticketTypeKey = "story";
        parentForMove.priorityKey = "medium";
        const auto moveParent = database.createTicket(parentForMove, demoUserId);

        CreateTicketRequest childForMove;
        childForMove.projectKey = "TH";
        childForMove.summary = "Child for move-rejection test";
        childForMove.ticketTypeKey = "sub-task";
        childForMove.priorityKey = "medium";
        childForMove.parentTicketKey = moveParent.key;
        const auto moveChild = database.createTicket(childForMove, demoUserId);

        bool moveTicketWithChildrenRejected = false;
        try {
            database.moveTicket(moveParent.key, "WEB", demoUserId);
        } catch (const std::invalid_argument&) {
            moveTicketWithChildrenRejected = true;
        }
        require(moveTicketWithChildrenRejected, "moving a ticket that has children is rejected");

        bool moveTicketWithParentRejected = false;
        try {
            database.moveTicket(moveChild.key, "WEB", demoUserId);
        } catch (const std::invalid_argument&) {
            moveTicketWithParentRejected = true;
        }
        require(moveTicketWithParentRejected, "moving a ticket that has a parent is rejected");

        // --- Per-user timezone/clock-format preferences (D45) ---
        {
            const auto beforeUpdate = database.findUserById(demoUserId);
            require(beforeUpdate.has_value() && beforeUpdate->timeZone == "UTC" && beforeUpdate->clockFormat == "24h",
                   "a freshly-seeded user defaults to UTC/24h");

            TicketHub::Domain::UpdatePreferencesRequest preferences;
            preferences.timeZone = "Europe/Prague";
            preferences.clockFormat = "12h";
            database.updateUserPreferences(demoUserId, preferences);

            const auto afterUpdate = database.findUserById(demoUserId);
            require(afterUpdate.has_value() && afterUpdate->timeZone == "Europe/Prague" && afterUpdate->clockFormat == "12h",
                   "updateUserPreferences persists both fields and findUserById reads them back");
        }

        // --- Changing an active project's key (D91) ---
        {
            const auto renamed = database.changeProjectKey("WEB", "SITE");
            require(renamed.has_value() && renamed->key == "SITE", "changeProjectKey returns the project under its new key");

            require(scalarInt(databasePath, "SELECT COUNT(*) FROM project_key_aliases WHERE alias_key = 'WEB'") == 1,
                   "the vacated project key is recorded as a permanent alias");

            const auto renamedTicket = database.findTicketByKey("SITE-3");
            require(renamedTicket.has_value(), "a ticket that belonged to WEB now resolves under the SITE prefix "
                   "with the same numeric suffix");
            require(renamedTicket->key == "SITE-3" && renamedTicket->projectKey == "SITE",
                   "the renamed ticket's key and projectKey both reflect the new prefix");

            const auto viaOldTicketKey = database.findTicketByKey("WEB-3");
            require(viaOldTicketKey.has_value() && viaOldTicketKey->key == "SITE-3",
                   "the ticket's old key (under the vacated project prefix) permanently resolves via ticket_key_aliases");
            require(scalarInt(databasePath, "SELECT COUNT(*) FROM ticket_key_aliases WHERE alias_key = 'WEB-3'") == 1,
                   "the renamed ticket's old key is recorded as a permanent alias");

            bool renameToCollidingProjectKeyRejected = false;
            try {
                database.changeProjectKey("TH", "SITE");
            } catch (const std::invalid_argument&) {
                renameToCollidingProjectKeyRejected = true;
            }
            require(renameToCollidingProjectKeyRejected, "renaming to a key already used by another active project is rejected");

            bool renameToAliasedProjectKeyRejected = false;
            try {
                database.changeProjectKey("TH", "WEB");
            } catch (const std::invalid_argument&) {
                renameToAliasedProjectKeyRejected = true;
            }
            require(renameToAliasedProjectKeyRejected,
                   "renaming to a key reserved by an existing project_key_aliases entry is rejected");

            const auto unknownProjectRename = database.changeProjectKey("NOPE", "NEWKEY");
            require(!unknownProjectRename.has_value(), "renaming an unknown project key returns nullopt");
        }

        require(database.softDeleteTicket(created.key, demoUserId), "a ticket can be soft-deleted");
        require(!database.findTicketByKey(created.key).has_value(),
               "a soft-deleted ticket is not found by ordinary lookup");
        require(!database.softDeleteTicket(created.key, demoUserId), "soft-deleting an already-deleted ticket is a no-op");

        const auto deletedTickets = database.listDeletedTickets();
        require(deletedTickets.size() == 1 && deletedTickets[0].key == created.key,
               "the deleted ticket appears in the recycle bin");

        require(database.restoreTicket(created.key), "the ticket can be restored");
        require(database.findTicketByKey(created.key).has_value(),
               "a restored ticket is found again by ordinary lookup");
        require(database.listDeletedTickets().empty(), "the recycle bin is empty again after restore");

        require(database.softDeleteTicket(created.key, demoUserId), "re-deleting for the permanent-delete test");
        require(database.permanentlyDeleteTicket(created.key), "the ticket can be permanently deleted");
        require(!database.permanentlyDeleteTicket(created.key),
               "permanently deleting an already-gone ticket returns false");
        require(scalarInt(databasePath, "SELECT COUNT(*) FROM comments WHERE ticket_id = '" + created.id + "'") == 0,
               "comments cascade-delete with the permanently-deleted ticket");
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

    // Backup/restore (Phase 7, D106-D108).
    const fs::path backupSourceDb = fs::temp_directory_path() / "ticket-hub-backup-source.db";
    const fs::path backupDir = fs::temp_directory_path() / "ticket-hub-backup-output";
    fs::remove(backupSourceDb, removeError);
    fs::remove(backupSourceDb.string() + "-wal", removeError);
    fs::remove(backupSourceDb.string() + "-shm", removeError);
    fs::remove_all(backupDir, removeError);
    {
        SqliteDatabase database(backupSourceDb.string(),
                                (sourceRoot / "migrations/sqlite").string(),
                                (sourceRoot / "migrations/sqlite/002_seed_demo.sql").string());
        database.migrate();
        database.seedDemoData();
        require(database.listTickets(TicketFilter{}).size() == 8, "backup source seeded with the demo ticket set");

        database.backup(backupDir.string());
        require(fs::exists(backupDir / "database.sqlite3"), "backup writes database.sqlite3 into the output directory");
        require(fs::file_size(backupDir / "database.sqlite3") > 0, "the backup file is not empty");

        // Mutate the live database after the backup was taken, so restoring
        // it is a meaningfully observable change, not a no-op.
        CreateTicketRequest postBackupTicket;
        postBackupTicket.projectKey = "TH";
        postBackupTicket.summary = "Created after the backup was taken";
        const auto createdAfterBackup = database.createTicket(postBackupTicket, demoUserId);
        require(database.listTickets(TicketFilter{}).size() == 9,
               "the live database now has one more ticket than the backup captured");

        database.restore(backupDir.string());
        require(database.listTickets(TicketFilter{}).size() == 8,
               "restore reverts the live database to exactly what the backup captured");
        require(!database.findTicketByKey(createdAfterBackup.key).has_value(),
               "a ticket created after the backup was taken does not survive a restore from that backup");
    }
    fs::remove(backupSourceDb, removeError);
    fs::remove(backupSourceDb.string() + "-wal", removeError);
    fs::remove(backupSourceDb.string() + "-shm", removeError);
    fs::remove_all(backupDir, removeError);

    std::cout << "SQLite integration tests passed\n";
    return 0;
}
