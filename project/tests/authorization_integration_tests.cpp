#include "application/TicketService.h"
#include "domain/Errors.h"
#include "infrastructure/database/SqliteDatabase.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
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

template <typename Action>
bool throwsForbidden(Action action) {
    try {
        action();
    } catch (const TicketHub::Domain::Forbidden&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using TicketHub::Application::TicketService;
    using TicketHub::Domain::CreateIssueRequest;
    using TicketHub::Domain::CreateProjectRequest;
    using TicketHub::Domain::EditIssueRequest;
    using TicketHub::Domain::Forbidden;
    using TicketHub::Domain::Principal;
    using TicketHub::Infrastructure::Database::SqliteDatabase;

    // Fixed seed identities and memberships from migrations/sqlite/002_seed_demo.sql:
    //   demo (global admin), alex (TH member, WEB admin), sam (TH member, no WEB membership).
    const Principal demo{"00000000-0000-4000-8000-000000000001", "demo@ticket-hub.local", "Demo User", true};
    const Principal alex{"00000000-0000-4000-8000-000000000002", "alex@ticket-hub.local", "Alex Morgan", false};
    const Principal sam{"00000000-0000-4000-8000-000000000003", "sam@ticket-hub.local", "Sam Lee", false};

    const fs::path sourceRoot(TICKETHUB_SOURCE_DIR);
    const fs::path databasePath = fs::temp_directory_path() / "ticket-hub-authorization.db";
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

    TicketService tickets(database);

    // --- Fixed project roles gate issue writes (D3) ---
    {
        CreateIssueRequest webIssue;
        webIssue.projectKey = "WEB";
        webIssue.summary = "Should be rejected";
        webIssue.description = "Sam is not a WEB project member.";

        require(throwsForbidden([&] { tickets.createIssue(webIssue, sam); }),
               "non-member cannot create an issue in another project");

        CreateIssueRequest thIssue;
        thIssue.projectKey = "TH";
        thIssue.summary = "Created by a TH member";
        thIssue.description = "Alex is a member of TH.";
        const auto created = tickets.createIssue(thIssue, alex);
        require(created.projectKey == "TH", "a TH member can create a TH issue");

        require(throwsForbidden([&] { tickets.changeStatus("WEB-1", "done", sam); }),
               "non-member cannot change status on another project's issue");
        require(throwsForbidden([&] { tickets.addComment("WEB-1", "hello", sam); }),
               "non-member cannot comment on another project's issue");

        // Alex is WEB's project admin (rank 2), which satisfies the member-rank
        // (1) requirement for ordinary writes too.
        require(tickets.changeStatus("WEB-1", "in-progress", alex), "a project admin can change status too");

        EditIssueRequest webEdit;
        webEdit.summary = "Should be rejected";
        webEdit.description = "Sam is not a WEB project member.";
        webEdit.priorityKey = "medium";
        require(throwsForbidden([&] { tickets.editIssue("WEB-1", webEdit, sam); }),
               "non-member cannot edit another project's issue");

        EditIssueRequest thEdit;
        thEdit.summary = "Edited by a TH member";
        thEdit.description = "Alex is a member of TH.";
        thEdit.priorityKey = "high";
        const auto edited = tickets.editIssue(created.key, thEdit, alex);
        require(edited.has_value() && edited->summary == thEdit.summary, "a TH member can edit a TH issue");

        EditIssueRequest missingEdit;
        missingEdit.summary = "n/a";
        missingEdit.priorityKey = "medium";
        require(!tickets.editIssue("TH-9999", missingEdit, demo).has_value(),
               "editing an unknown issue returns nullopt rather than throwing");
    }

    // --- Cloning and issue links respect the same fixed project roles (D3, D17, D60) ---
    {
        require(throwsForbidden([&] { tickets.cloneIssue("WEB-1", sam); }),
               "non-member cannot clone another project's issue");
        const auto webClone = tickets.cloneIssue("WEB-1", alex);
        require(webClone.projectKey == "WEB", "a project admin can clone a project issue");

        require(throwsForbidden([&] {
            tickets.createIssueLink("TH-1", "WEB-1", TicketHub::Domain::LinkTypeRelatesTo, sam);
        }), "linking requires access to both projects, even when the source project is accessible");

        const auto link = tickets.createIssueLink("TH-1", "WEB-1", TicketHub::Domain::LinkTypeRelatesTo, demo);
        require(throwsForbidden([&] { tickets.deleteIssueLink(link.id, sam); }),
               "non-member of either linked project cannot delete the link");
        require(tickets.deleteIssueLink(link.id, alex),
               "a member of both linked projects (TH member, WEB admin) can delete the link");
    }

    // --- Manual ordering and moving between projects respect the same fixed project roles (D31, D37) ---
    {
        require(throwsForbidden([&] { tickets.reorderIssue("WEB-1", std::nullopt, sam); }),
               "non-member cannot reorder another project's issue");

        const auto reordered = tickets.reorderIssue("TH-1", std::string("TH-2"), alex);
        require(reordered.key == "TH-1", "a TH member can reorder a TH issue");

        CreateIssueRequest moveTarget;
        moveTarget.projectKey = "TH";
        moveTarget.summary = "Move-authorization test issue";
        const auto toMove = tickets.createIssue(moveTarget, demo);

        require(throwsForbidden([&] { tickets.moveIssue(toMove.key, "WEB", sam); }),
               "moving requires access to the target project; sam is not a WEB member");
        require(throwsForbidden([&] { tickets.moveIssue("WEB-1", "TH", sam); }),
               "moving also requires access to the source project");

        const auto moved = tickets.moveIssue(toMove.key, "WEB", alex);
        require(moved.projectKey == "WEB", "a member of both projects (TH member, WEB admin) can move an issue");
    }

    // --- Comment editing and tombstone delete: simplified author-or-admin permissions (D81/D82/D83) ---
    {
        // Sam and alex are both plain TH members (rank 1); demo is the global
        // administrator. Neither sam nor alex is a TH project admin (rank 2).
        const auto samComment = tickets.addComment("TH-1", "Comment by sam", sam);

        require(throwsForbidden([&] {
            tickets.editComment("TH-1", samComment.id, "alex trying to edit sam's comment", alex);
        }), "a non-author, non-admin project member cannot edit someone else's comment");
        require(throwsForbidden([&] { tickets.deleteComment("TH-1", samComment.id, alex); }),
               "a non-author, non-admin project member cannot delete someone else's comment");

        const auto selfEdited = tickets.editComment("TH-1", samComment.id, "edited by the author", sam);
        require(selfEdited.has_value() && selfEdited->body == "edited by the author" && selfEdited->editedAt.has_value(),
               "the comment's own author can always edit it");

        // The global administrator can edit/delete any comment (D83).
        const auto adminEdited = tickets.editComment("TH-1", samComment.id, "edited by the global admin", demo);
        require(adminEdited.has_value() && adminEdited->body == "edited by the global admin",
               "a global administrator can edit another user's comment");
        require(tickets.deleteComment("TH-1", samComment.id, demo),
               "a global administrator can delete another user's comment");

        require(!tickets.editComment("TH-1", "00000000-0000-4000-8000-00000000dead", "n/a", demo).has_value(),
               "editing an unknown comment returns nullopt rather than throwing");
        require(!tickets.deleteComment("TH-1", "00000000-0000-4000-8000-00000000dead", demo),
               "deleting an unknown comment returns false rather than throwing");
    }

    // --- Watching and voting are self-service and require no project role (D20, D79) ---
    {
        // Sam is not a WEB member at all, unlike every other write tested
        // above -- watch/vote are the one exception to "roles gate writes".
        require(tickets.watchIssue("WEB-1", sam), "a non-member can still watch an issue");
        require(!tickets.watchIssue("WEB-1", sam), "watching again is a no-op");
        require(tickets.listWatchers("WEB-1", demo).size() == 1, "the watcher is visible to any authenticated reader");
        require(tickets.unwatchIssue("WEB-1", sam), "a non-member can unwatch their own watch");

        require(tickets.voteIssue("WEB-1", sam), "a non-member can still vote on an issue");
        require(!tickets.voteIssue("WEB-1", sam), "voting again is a no-op");
        require(tickets.listVoters("WEB-1", demo).size() == 1, "the voter is visible to any authenticated reader");
        require(tickets.unvoteIssue("WEB-1", sam), "a non-member can remove their own vote");
    }

    // --- Fixed emoji reactions on comments are self-service and require no project role (D84) ---
    {
        const auto reactionComment = tickets.addComment("WEB-1", "Comment for reaction tests", alex);

        require(tickets.addCommentReaction("WEB-1", reactionComment.id, "thumbs_up", sam),
               "a non-member can still react to a comment");
        require(!tickets.addCommentReaction("WEB-1", reactionComment.id, "thumbs_up", sam),
               "reacting again with the same key is a no-op");
        require(tickets.listCommentReactions("WEB-1", reactionComment.id, demo).size() == 1,
               "the reaction is visible to any authenticated reader");
        require(tickets.removeCommentReaction("WEB-1", reactionComment.id, "thumbs_up", sam),
               "a non-member can remove their own reaction");

        bool invalidReactionKeyRejected = false;
        try {
            tickets.addCommentReaction("WEB-1", reactionComment.id, "not_a_real_reaction", sam);
        } catch (const std::invalid_argument&) {
            invalidReactionKeyRejected = true;
        }
        require(invalidReactionKeyRejected, "an unknown reaction key is rejected");

        bool unknownCommentRejectedOnAdd = false;
        try {
            tickets.addCommentReaction("WEB-1", "00000000-0000-4000-8000-00000000dead", "thumbs_up", sam);
        } catch (const std::invalid_argument&) {
            unknownCommentRejectedOnAdd = true;
        }
        require(unknownCommentRejectedOnAdd, "reacting to an unknown comment is rejected");

        bool unknownCommentRejectedOnRemove = false;
        try {
            tickets.removeCommentReaction("WEB-1", "00000000-0000-4000-8000-00000000dead", "thumbs_up", sam);
        } catch (const std::invalid_argument&) {
            unknownCommentRejectedOnRemove = true;
        }
        require(unknownCommentRejectedOnRemove, "un-reacting to an unknown comment is rejected");
    }

    // --- Fixed in-app notifications (D14): assigned, mentioned, watched-comment ---
    // Each sub-test uses its own issue and resets with markAllNotificationsRead
    // so later side effects (e.g. a lingering watcher from an earlier
    // sub-test) can never contaminate a later assertion.
    {
        // --- assigned ---
        CreateIssueRequest assignedToAlex;
        assignedToAlex.projectKey = "TH";
        assignedToAlex.summary = "Notification test: assigned";
        assignedToAlex.assigneeEmail = "alex@ticket-hub.local";
        const auto assignedIssue = tickets.createIssue(assignedToAlex, demo);
        require(tickets.countUnreadNotifications(alex) == 1,
               "creating an issue assigned to alex notifies alex (D14 assigned)");

        CreateIssueRequest selfAssigned;
        selfAssigned.projectKey = "TH";
        selfAssigned.summary = "Notification test: self-assigned";
        selfAssigned.assigneeEmail = "demo@ticket-hub.local";
        tickets.createIssue(selfAssigned, demo);
        require(tickets.countUnreadNotifications(demo) == 0, "assigning an issue to yourself does not notify you");

        EditIssueRequest reassign;
        reassign.summary = assignedIssue.summary;
        reassign.description = assignedIssue.description;
        reassign.priorityKey = assignedIssue.priority.key;
        reassign.assigneeEmail = "alex@ticket-hub.local"; // unchanged
        tickets.editIssue(assignedIssue.key, reassign, demo);
        require(tickets.countUnreadNotifications(alex) == 1,
               "re-saving an edit with the same assignee does not send a second notification");

        reassign.assigneeEmail = "sam@ticket-hub.local"; // actually changed
        tickets.editIssue(assignedIssue.key, reassign, demo);
        require(tickets.countUnreadNotifications(sam) == 1,
               "reassigning to a different user on edit notifies the new assignee");
        require(tickets.markAllNotificationsRead(alex) && tickets.markAllNotificationsRead(sam),
               "reset before the next sub-test");

        // --- mentioned ---
        CreateIssueRequest mentionIssue;
        mentionIssue.projectKey = "TH";
        mentionIssue.summary = "Notification test: mentioned";
        const auto mentioned = tickets.createIssue(mentionIssue, demo);
        tickets.addComment(mentioned.key, "@sam please take a look at this.", alex);
        require(tickets.countUnreadNotifications(sam) == 1, "an @handle mention notifies that user");

        // An unknown handle is silently ignored -- no throw, no notification.
        tickets.addComment(mentioned.key, "@no-such-handle-at-all, thanks!", alex);
        require(tickets.countUnreadNotifications(demo) == 0 && tickets.countUnreadNotifications(sam) == 1,
               "an unknown @handle notifies nobody and does not throw");

        // Mentioning yourself never notifies you.
        tickets.addComment(mentioned.key, "@alex noting this for myself.", alex);
        require(tickets.countUnreadNotifications(alex) == 0, "mentioning yourself does not notify you");
        require(tickets.markAllNotificationsRead(sam), "reset before the next sub-test");

        // --- watched_comment, and the "mentioned wins over watched" dedupe ---
        CreateIssueRequest watchedIssue;
        watchedIssue.projectKey = "TH";
        watchedIssue.summary = "Notification test: watched comment";
        const auto watched = tickets.createIssue(watchedIssue, demo);
        tickets.watchIssue(watched.key, sam);
        tickets.addComment(watched.key, "Progress update, no mentions here.", alex);
        require(tickets.countUnreadNotifications(sam) == 1,
               "a new comment on a watched issue notifies every watcher except its author");

        // The comment author never gets a watched-comment notification for
        // their own comment, even while watching the issue themselves.
        tickets.watchIssue(watched.key, alex);
        tickets.addComment(watched.key, "Another update.", alex);
        require(tickets.countUnreadNotifications(alex) == 0,
               "the comment's own author is never notified about their own comment");
        require(tickets.countUnreadNotifications(sam) == 2, "sam (a different watcher) is notified again");

        require(tickets.markAllNotificationsRead(sam), "reset before the dedupe sub-test");
        tickets.addComment(watched.key, "@sam this one both mentions you and you're watching.", alex);
        const auto dedupedNotifications = tickets.listNotifications(sam, true);
        require(dedupedNotifications.size() == 1,
               "being both mentioned and a watcher on the same comment yields exactly one notification");
        require(dedupedNotifications.front().type == "mentioned",
               "the more specific reason (mentioned) wins over the generic watched-comment one");

        // --- listNotifications / markNotificationRead / markAllNotificationsRead scoping ---
        const auto samNotifications = tickets.listNotifications(sam, false);
        require(!samNotifications.empty(), "listNotifications (all) returns sam's history, not just unread");
        const auto firstId = samNotifications.front().id;
        require(tickets.markNotificationRead(firstId, sam), "sam can mark their own notification read");
        require(!tickets.markNotificationRead(firstId, demo),
               "demo cannot mark sam's notification read (scoped to the caller)");
        require(tickets.countUnreadNotifications(sam) == 0,
               "marking the single remaining unread notification read leaves none unread");
        require(!tickets.markAllNotificationsRead(sam),
               "markAllNotificationsRead is a no-op (returns false) once everything is already read");
    }

    // --- Simplified worklogs: no own-vs-others permission split (D12/D13) ---
    {
        require(throwsForbidden([&] { tickets.addWorklog("WEB-1", "2026-07-30", 3600, std::nullopt, sam); }),
               "a non-member cannot log work on another project's issue");

        const auto samWorklog = tickets.addWorklog("TH-1", "2026-07-30", 3600, std::string("Sam's work"), sam);
        require(samWorklog.timeSpentSeconds == 3600, "a TH member can log work on a TH issue");

        // Unlike comments (D83's author-or-admin rule), any project member
        // may edit or delete *anyone's* worklog -- D13 deliberately dropped
        // the own-vs-others split.
        const auto editedByAlex = tickets.editWorklog("TH-1", samWorklog.id, "2026-07-31", 7200,
                                                       std::string("Edited by alex"), alex);
        require(editedByAlex.has_value() && editedByAlex->timeSpentSeconds == 7200,
               "a different project member can edit someone else's worklog (no own-vs-others split)");

        require(throwsForbidden([&] { tickets.editWorklog("WEB-1", "00000000-0000-4000-8000-00000000dead", "2026-07-31", 100, std::nullopt, sam); }),
               "the project-role check runs before the worklog lookup, so a non-member is still rejected "
               "even for a worklog id that doesn't exist");

        require(tickets.deleteWorklog("TH-1", samWorklog.id, alex),
               "a different project member can delete someone else's worklog");
        const auto worklogsAfterDelete = tickets.listWorklogs("TH-1", demo);
        require(std::none_of(worklogsAfterDelete.begin(), worklogsAfterDelete.end(),
                             [&](const auto& w) { return w.id == samWorklog.id; }),
               "the deleted worklog no longer appears in the list");

        require(!tickets.editWorklog("TH-1", "00000000-0000-4000-8000-00000000dead", "2026-07-30", 100, std::nullopt, demo)
                   .has_value(),
               "editing an unknown worklog returns nullopt rather than throwing");
        require(!tickets.deleteWorklog("TH-1", "00000000-0000-4000-8000-00000000dead", demo),
               "deleting an unknown worklog returns false rather than throwing");
    }

    // --- User directory for @mention autocomplete (D80) ---
    {
        const auto users = tickets.listUsers(sam);
        require(users.size() >= 3, "listUsers returns at least the three seeded demo users");
        require(std::any_of(users.begin(), users.end(),
                            [](const auto& u) { return u.handle.has_value() && *u.handle == "alex"; }),
               "the seeded demo users have their handles populated");
    }

    // --- Issue recycle bin: project-admin-vs-global-admin split (D22, mirrors D88) ---
    {
        CreateIssueRequest binRequest;
        binRequest.projectKey = "WEB";
        binRequest.summary = "Recycle bin test issue";
        const auto issue = tickets.createIssue(binRequest, alex); // alex is WEB admin

        require(throwsForbidden([&] { tickets.deleteIssue(issue.key, sam); }),
               "a non-member cannot soft-delete an issue");
        require(tickets.deleteIssue(issue.key, alex), "a project admin can soft-delete an issue");

        require(throwsForbidden([&] { tickets.listDeletedIssues(alex); }),
               "listing the issue recycle bin is global-administrator-only");
        {
            const auto deleted = tickets.listDeletedIssues(demo);
            const auto match = std::find_if(deleted.begin(), deleted.end(),
                                             [&](const auto& candidate) { return candidate.key == issue.key; });
            require(match != deleted.end(), "the global administrator sees the deleted issue in the recycle bin");
        }

        require(throwsForbidden([&] { tickets.restoreIssue(issue.key, alex); }),
               "restoring an issue from the recycle bin is global-administrator-only");
        require(tickets.restoreIssue(issue.key, demo), "the global administrator can restore the issue");

        require(tickets.deleteIssue(issue.key, alex), "re-deleting for the permanent-delete test");
        require(throwsForbidden([&] { tickets.permanentlyDeleteIssue(issue.key, alex); }),
               "permanently deleting an issue is global-administrator-only");
        require(tickets.permanentlyDeleteIssue(issue.key, demo),
               "the global administrator can permanently delete the issue");
    }

    // --- Simple bulk actions apply the same authorization/validation per issue (D36) ---
    {
        CreateIssueRequest thBulk;
        thBulk.projectKey = "TH";
        thBulk.summary = "Bulk 1";
        const auto bulk1 = tickets.createIssue(thBulk, demo);
        thBulk.summary = "Bulk 2";
        const auto bulk2 = tickets.createIssue(thBulk, demo);

        // Sam is a TH member but not a WEB member; TH-9999 does not exist.
        const std::vector<std::string> mixedKeys = {bulk1.key, bulk2.key, "WEB-1", "TH-9999"};
        const auto assignResult = tickets.bulkAssign(mixedKeys, std::string("alex@ticket-hub.local"), sam);
        require(assignResult.succeeded.size() == 2, "bulk assign succeeds for the two accessible TH issues");
        require(assignResult.failed.size() == 2, "bulk assign reports the inaccessible and unknown issues as failed");

        const auto labelResult = tickets.bulkAddLabel({bulk1.key, bulk2.key}, "bulk-tested", demo);
        require(labelResult.succeeded.size() == 2, "bulk label succeeds for both issues");
        {
            const auto found = tickets.findIssue(bulk1.key, demo);
            require(found.has_value() && !found->labels.empty() && found->labels.front() == "bulk-tested",
                   "the bulk-added label is applied");
        }

        const auto statusResult = tickets.bulkChangeStatus({bulk1.key, bulk2.key}, "in-progress", std::nullopt, demo);
        require(statusResult.succeeded.size() == 2, "bulk status change succeeds for both issues");

        const auto deleteResult = tickets.bulkDelete({bulk1.key, bulk2.key}, demo);
        require(deleteResult.succeeded.size() == 2, "bulk delete (recycle) succeeds for both issues");
        require(!tickets.findIssue(bulk1.key, demo).has_value(), "a bulk-deleted issue is no longer found");
    }

    // --- Anonymous read-access toggle (D59, off by default) ---
    {
        const std::optional<Principal> anonymous = std::nullopt;
        require(!tickets.isAnonymousReadEnabled(), "anonymous read access is disabled by default");

        bool anonymousReadRejected = false;
        try {
            tickets.listProjects(anonymous);
        } catch (const TicketHub::Domain::AuthenticationRequired&) {
            anonymousReadRejected = true;
        }
        require(anonymousReadRejected, "an anonymous caller is rejected while the toggle is off");
        require(!tickets.listProjects(demo).empty(), "an authenticated caller can always read, regardless of the toggle");

        require(throwsForbidden([&] { tickets.setAnonymousReadEnabled(true, alex); }),
               "only a global administrator may flip the anonymous-read toggle");

        tickets.setAnonymousReadEnabled(true, demo);
        require(tickets.isAnonymousReadEnabled(), "the toggle takes effect");
        require(!tickets.listProjects(anonymous).empty(), "an anonymous caller can read once the toggle is on");

        tickets.setAnonymousReadEnabled(false, demo);
        require(!tickets.isAnonymousReadEnabled(), "the toggle can be turned back off");
    }

    // --- Not-found behavior is unaffected by authorization (global admin actor) ---
    {
        require(!tickets.changeStatus("TH-9999", "done", demo), "changing status of an unknown issue returns false");
        bool notFoundThrew = false;
        try {
            tickets.addComment("TH-9999", "hello", demo);
        } catch (const std::invalid_argument&) {
            notFoundThrew = true;
        }
        require(notFoundThrew, "commenting on an unknown issue reports invalid_argument, not Forbidden");
    }

    // --- Project lifecycle (D3/D87/D88/D89) ---
    {
        require(throwsForbidden([&] {
            tickets.createProject(CreateProjectRequest{"QA", "Quality", "Should be rejected"}, alex);
        }), "a non-global-administrator cannot create a project, even as another project's admin");

        const auto qa = tickets.createProject(CreateProjectRequest{"QA", "Quality", "Created by an admin"}, demo);
        require(qa.key == "QA" && !qa.archived, "a global administrator can create a project");

        require(throwsForbidden([&] { tickets.setProjectArchived("QA", true, alex); }),
               "a user with no membership in QA cannot archive it");

        require(tickets.setProjectArchived("QA", true, demo), "the creator (global admin) can archive QA");
        {
            // D87: archiving is read-only, but it also "leaves active lists" --
            // listProjects() is the active-projects view, so an archived
            // project drops out of it rather than showing archived=true.
            const auto projects = tickets.listProjects(demo);
            const auto match = std::find_if(projects.begin(), projects.end(),
                                             [](const auto& project) { return project.key == "QA"; });
            require(match == projects.end(), "archiving QA removes it from the active project list");
        }
        require(tickets.setProjectArchived("QA", false, demo), "QA can be unarchived");
        {
            const auto projects = tickets.listProjects(demo);
            const auto match = std::find_if(projects.begin(), projects.end(),
                                             [](const auto& project) { return project.key == "QA"; });
            require(match != projects.end() && !match->archived, "unarchiving QA restores it to the active project list");
        }

        // Promote Alex to QA project admin (without global admin) to exercise
        // the "project admin, not global admin" path for the rest of the
        // lifecycle actions.
        executeSql(databasePath, R"SQL(
UPDATE projects SET archived = 0 WHERE project_key = 'QA';
INSERT INTO project_members(project_id, user_id, role_key)
SELECT id, '00000000-0000-4000-8000-000000000002', 'admin' FROM projects WHERE project_key = 'QA';
)SQL");

        require(tickets.deleteProject("QA", alex), "a project admin (not global admin) can soft-delete their project");

        require(throwsForbidden([&] { tickets.listDeletedProjects(alex); }),
               "listing the recycle bin is global-administrator-only");
        {
            const auto deleted = tickets.listDeletedProjects(demo);
            const auto match = std::find_if(deleted.begin(), deleted.end(),
                                             [](const auto& project) { return project.key == "QA"; });
            require(match != deleted.end(), "the global administrator sees QA in the recycle bin");
        }

        require(throwsForbidden([&] { tickets.restoreProject("QA", alex); }),
               "restoring from the recycle bin is global-administrator-only");
        require(tickets.restoreProject("QA", demo), "the global administrator can restore QA");

        require(tickets.deleteProject("QA", alex), "QA can be soft-deleted again for the permanent-delete test");
        require(throwsForbidden([&] { tickets.permanentlyDeleteProject("QA", alex); }),
               "permanent deletion is global-administrator-only");
        require(tickets.permanentlyDeleteProject("QA", demo), "the global administrator can permanently delete QA");
        require(!tickets.permanentlyDeleteProject("QA", demo),
               "permanently deleting an already-gone project returns false");
    }

    // --- Simple append-only admin/security audit log (D23) ---
    // By this point in the file, permanentlyDeleteIssue, setAnonymousReadEnabled
    // (twice), and permanentlyDeleteProject have all already run above.
    {
        require(throwsForbidden([&] { tickets.listAuditEvents(sam); }),
               "reading the audit log is global-administrator-only");

        const auto events = tickets.listAuditEvents(demo);
        require(std::any_of(events.begin(), events.end(),
                            [](const auto& e) { return e.category == "admin" && e.action == "issue.permanently_deleted"; }),
               "permanentlyDeleteIssue recorded an admin/issue.permanently_deleted audit event");
        require(std::any_of(events.begin(), events.end(),
                            [](const auto& e) {
                                return e.category == "admin" && e.action == "settings.anonymous_read_changed";
                            }),
               "setAnonymousReadEnabled recorded an admin/settings.anonymous_read_changed audit event");
        require(std::any_of(events.begin(), events.end(),
                            [](const auto& e) { return e.category == "admin" && e.action == "project.permanently_deleted"; }),
               "permanentlyDeleteProject recorded an admin/project.permanently_deleted audit event");
        require(std::all_of(events.begin(), events.end(),
                            [](const auto& e) { return e.actor.has_value() && e.actor->id == "00000000-0000-4000-8000-000000000001"; }),
               "every admin-triggered event in this run was attributed to demo, the actor who performed each action");
    }

    // --- Fixed personal dashboard (D24) ---
    // By this point TH-1 (Done, assigned to alex), TH-3 (In Review, assigned
    // to alex), and WEB-1 (In Progress, assigned to alex, unchanged by the
    // sam-forbidden bulk-assign attempt above) are all still in their
    // original seeded state.
    {
        const std::optional<Principal> anonymous = std::nullopt;
        tickets.setAnonymousReadEnabled(true, demo);
        const auto anonDashboard = tickets.dashboard(anonymous);
        tickets.setAnonymousReadEnabled(false, demo);
        require(anonDashboard.assignedToMe.empty() && anonDashboard.watchedIssues.empty()
                    && anonDashboard.upcomingDeadlines.empty(),
               "an anonymous viewer's dashboard has no personal widgets, even when anonymous read is enabled");

        const auto alexDashboard = tickets.dashboard(alex);
        require(alexDashboard.assignedToMe.size() == 2,
               "alex's assigned-to-me widget excludes the Done-category TH-1, keeping TH-3 and WEB-1");
        require(std::none_of(alexDashboard.assignedToMe.begin(), alexDashboard.assignedToMe.end(),
                             [](const auto& issue) { return issue.key == "TH-1"; }),
               "a Done-category issue does not appear in assigned-to-me");
        require(std::any_of(alexDashboard.assignedToMe.begin(), alexDashboard.assignedToMe.end(),
                            [](const auto& issue) { return issue.key == "TH-3"; }),
               "an open issue assigned to the actor appears in assigned-to-me");
        require(std::none_of(alexDashboard.watchedIssues.begin(), alexDashboard.watchedIssues.end(),
                             [](const auto& issue) { return issue.key == "TH-2"; }),
               "alex is not watching TH-2 yet (alex is already watching an earlier notification-test issue)");

        require(tickets.watchIssue("TH-2", alex), "alex watches TH-2 for the dashboard test");
        const auto alexDashboardAfterWatch = tickets.dashboard(alex);
        require(std::any_of(alexDashboardAfterWatch.watchedIssues.begin(), alexDashboardAfterWatch.watchedIssues.end(),
                            [](const auto& issue) { return issue.key == "TH-2"; }),
               "the watched-issues widget reflects a newly watched issue");
        require(tickets.unwatchIssue("TH-2", alex), "cleanup: alex unwatches TH-2");
    }

    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    std::cout << "Authorization integration tests passed\n";
    return 0;
}
