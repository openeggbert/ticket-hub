#include "application/TicketService.h"
#include "domain/Errors.h"
#include "domain/Validation.h"
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
    using TicketHub::Domain::CreateTicketRequest;
    using TicketHub::Domain::CreateProjectRequest;
    using TicketHub::Domain::EditTicketRequest;
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

    const fs::path attachmentsRoot = fs::temp_directory_path() / "ticket-hub-authorization-attachments";
    fs::remove_all(attachmentsRoot, removeError);

    TicketService tickets(database, attachmentsRoot.string());

    // --- Fixed project roles gate ticket writes (D3) ---
    {
        CreateTicketRequest webTicket;
        webTicket.projectKey = "WEB";
        webTicket.summary = "Should be rejected";
        webTicket.description = "Sam is not a WEB project member.";

        require(throwsForbidden([&] { tickets.createTicket(webTicket, sam); }),
               "non-member cannot create a ticket in another project");

        CreateTicketRequest thTicket;
        thTicket.projectKey = "TH";
        thTicket.summary = "Created by a TH member";
        thTicket.description = "Alex is a member of TH.";
        const auto created = tickets.createTicket(thTicket, alex);
        require(created.projectKey == "TH", "a TH member can create a TH ticket");

        require(throwsForbidden([&] { tickets.changeStatus("WEB-1", "done", sam); }),
               "non-member cannot change status on another project's ticket");
        require(throwsForbidden([&] { tickets.addComment("WEB-1", "hello", sam); }),
               "non-member cannot comment on another project's ticket");

        // Alex is WEB's project admin (rank 2), which satisfies the member-rank
        // (1) requirement for ordinary writes too.
        require(tickets.changeStatus("WEB-1", "in-progress", alex), "a project admin can change status too");

        EditTicketRequest webEdit;
        webEdit.summary = "Should be rejected";
        webEdit.description = "Sam is not a WEB project member.";
        webEdit.priorityKey = "medium";
        webEdit.ticketTypeKey = "task";
        require(throwsForbidden([&] { tickets.editTicket("WEB-1", webEdit, sam); }),
               "non-member cannot edit another project's ticket");

        EditTicketRequest thEdit;
        thEdit.summary = "Edited by a TH member";
        thEdit.description = "Alex is a member of TH.";
        thEdit.priorityKey = "high";
        thEdit.ticketTypeKey = created.type.key;
        const auto edited = tickets.editTicket(created.key, thEdit, alex);
        require(edited.has_value() && edited->summary == thEdit.summary, "a TH member can edit a TH ticket");

        EditTicketRequest missingEdit;
        missingEdit.summary = "n/a";
        missingEdit.priorityKey = "medium";
        missingEdit.ticketTypeKey = "task";
        require(!tickets.editTicket("TH-9999", missingEdit, demo).has_value(),
               "editing an unknown ticket returns nullopt rather than throwing");
    }

    // --- Cloning and ticket links respect the same fixed project roles (D3, D17, D60) ---
    {
        require(throwsForbidden([&] { tickets.cloneTicket("WEB-1", sam); }),
               "non-member cannot clone another project's ticket");
        const auto webClone = tickets.cloneTicket("WEB-1", alex);
        require(webClone.projectKey == "WEB", "a project admin can clone a project ticket");

        require(throwsForbidden([&] {
            tickets.createTicketLink("TH-1", "WEB-1", TicketHub::Domain::LinkTypeRelatesTo, sam);
        }), "linking requires access to both projects, even when the source project is accessible");

        const auto link = tickets.createTicketLink("TH-1", "WEB-1", TicketHub::Domain::LinkTypeRelatesTo, demo);
        require(throwsForbidden([&] { tickets.deleteTicketLink(link.id, sam); }),
               "non-member of either linked project cannot delete the link");
        require(tickets.deleteTicketLink(link.id, alex),
               "a member of both linked projects (TH member, WEB admin) can delete the link");
    }

    // --- Manual ordering and moving between projects respect the same fixed project roles (D31, D37) ---
    {
        require(throwsForbidden([&] { tickets.reorderTicket("WEB-1", std::nullopt, sam); }),
               "non-member cannot reorder another project's ticket");

        const auto reordered = tickets.reorderTicket("TH-1", std::string("TH-2"), alex);
        require(reordered.key == "TH-1", "a TH member can reorder a TH ticket");

        CreateTicketRequest moveTarget;
        moveTarget.projectKey = "TH";
        moveTarget.summary = "Move-authorization test ticket";
        const auto toMove = tickets.createTicket(moveTarget, demo);

        require(throwsForbidden([&] { tickets.moveTicket(toMove.key, "WEB", sam); }),
               "moving requires access to the target project; sam is not a WEB member");
        require(throwsForbidden([&] { tickets.moveTicket("WEB-1", "TH", sam); }),
               "moving also requires access to the source project");

        const auto moved = tickets.moveTicket(toMove.key, "WEB", alex);
        require(moved.projectKey == "WEB", "a member of both projects (TH member, WEB admin) can move a ticket");
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

        // Regression test for a real IDOR found in the Phase 8 security
        // self-review: editComment/deleteComment used to resolve the
        // project-role check from the URL's ticketKey but look up the
        // comment purely by id, so an admin of *some* project could
        // edit/delete a comment that actually belongs to a *different*
        // project's ticket, as long as the URL named a ticket they do
        // control. alex is TH member / WEB admin but did not author this
        // TH comment; passing WEB-1 (where alex holds Admin rank) as the
        // URL ticketKey must not let alex reach a TH comment by id, even
        // though alex is only a plain TH member (not TH admin) on the
        // project the comment actually belongs to.
        const auto thComment = tickets.addComment("TH-1", "Comment on a TH ticket", sam);
        require(!tickets.editComment("WEB-1", thComment.id, "cross-ticket edit attempt", alex).has_value(),
               "a comment cannot be edited through an unrelated ticket's URL, even by that ticket's admin");
        require(!tickets.deleteComment("WEB-1", thComment.id, alex),
               "a comment cannot be deleted through an unrelated ticket's URL, even by that ticket's admin");
        // Addressed through its own (TH) ticket URL, alex is correctly still
        // rejected (only a plain TH member, not TH admin, and not the
        // author) -- confirming the mismatch check above isn't just
        // masking a role check that would have failed anyway for a
        // different reason.
        require(throwsForbidden([&] { tickets.deleteComment("TH-1", thComment.id, alex); }),
               "...and the comment is still protected by the ordinary role check when addressed correctly");
        require(tickets.deleteComment("TH-1", thComment.id, demo),
               "a global administrator can still delete it through the correct ticket URL");
    }

    // --- Watching and voting are self-service and require no project role (D20, D79) ---
    {
        // Sam is not a WEB member at all, unlike every other write tested
        // above -- watch/vote are the one exception to "roles gate writes".
        require(tickets.watchTicket("WEB-1", sam), "a non-member can still watch a ticket");
        require(!tickets.watchTicket("WEB-1", sam), "watching again is a no-op");
        require(tickets.listWatchers("WEB-1", demo).size() == 1, "the watcher is visible to any authenticated reader");
        require(tickets.unwatchTicket("WEB-1", sam), "a non-member can unwatch their own watch");

        require(tickets.voteTicket("WEB-1", sam), "a non-member can still vote on a ticket");
        require(!tickets.voteTicket("WEB-1", sam), "voting again is a no-op");
        require(tickets.listVoters("WEB-1", demo).size() == 1, "the voter is visible to any authenticated reader");
        require(tickets.unvoteTicket("WEB-1", sam), "a non-member can remove their own vote");
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
    // Each sub-test uses its own ticket and resets with markAllNotificationsRead
    // so later side effects (e.g. a lingering watcher from an earlier
    // sub-test) can never contaminate a later assertion.
    {
        // --- assigned ---
        CreateTicketRequest assignedToAlex;
        assignedToAlex.projectKey = "TH";
        assignedToAlex.summary = "Notification test: assigned";
        assignedToAlex.assigneeEmail = "alex@ticket-hub.local";
        const auto assignedTicket = tickets.createTicket(assignedToAlex, demo);
        require(tickets.countUnreadNotifications(alex) == 1,
               "creating a ticket assigned to alex notifies alex (D14 assigned)");

        CreateTicketRequest selfAssigned;
        selfAssigned.projectKey = "TH";
        selfAssigned.summary = "Notification test: self-assigned";
        selfAssigned.assigneeEmail = "demo@ticket-hub.local";
        tickets.createTicket(selfAssigned, demo);
        require(tickets.countUnreadNotifications(demo) == 0, "assigning a ticket to yourself does not notify you");

        EditTicketRequest reassign;
        reassign.summary = assignedTicket.summary;
        reassign.description = assignedTicket.description;
        reassign.priorityKey = assignedTicket.priority.key;
        reassign.ticketTypeKey = assignedTicket.type.key;
        reassign.assigneeEmail = "alex@ticket-hub.local"; // unchanged
        tickets.editTicket(assignedTicket.key, reassign, demo);
        require(tickets.countUnreadNotifications(alex) == 1,
               "re-saving an edit with the same assignee does not send a second notification");

        reassign.assigneeEmail = "sam@ticket-hub.local"; // actually changed
        tickets.editTicket(assignedTicket.key, reassign, demo);
        require(tickets.countUnreadNotifications(sam) == 1,
               "reassigning to a different user on edit notifies the new assignee");
        require(tickets.markAllNotificationsRead(alex) && tickets.markAllNotificationsRead(sam),
               "reset before the next sub-test");

        // --- mentioned ---
        CreateTicketRequest mentionTicket;
        mentionTicket.projectKey = "TH";
        mentionTicket.summary = "Notification test: mentioned";
        const auto mentioned = tickets.createTicket(mentionTicket, demo);
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
        CreateTicketRequest watchedTicket;
        watchedTicket.projectKey = "TH";
        watchedTicket.summary = "Notification test: watched comment";
        const auto watched = tickets.createTicket(watchedTicket, demo);
        tickets.watchTicket(watched.key, sam);
        tickets.addComment(watched.key, "Progress update, no mentions here.", alex);
        require(tickets.countUnreadNotifications(sam) == 1,
               "a new comment on a watched ticket notifies every watcher except its author");

        // The comment author never gets a watched-comment notification for
        // their own comment, even while watching the ticket themselves.
        tickets.watchTicket(watched.key, alex);
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
               "a non-member cannot log work on another project's ticket");

        const auto samWorklog = tickets.addWorklog("TH-1", "2026-07-30", 3600, std::string("Sam's work"), sam);
        require(samWorklog.timeSpentSeconds == 3600, "a TH member can log work on a TH ticket");

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

        // Regression test for the same class of IDOR as the comment test
        // above: editWorklog/deleteWorklog checked the project-role from
        // the URL's ticketKey but looked the worklog up purely by id. Sam is
        // a TH member with *no* WEB membership at all, so sam must not be
        // able to reach a WEB worklog by routing the request through a TH
        // ticket URL (where sam does hold Member rank).
        const auto webWorklog = tickets.addWorklog("WEB-1", "2026-07-30", 1800, std::string("Alex's WEB work"), alex);
        require(!tickets.editWorklog("TH-1", webWorklog.id, "2026-07-31", 900, std::nullopt, sam).has_value(),
               "a worklog cannot be edited through an unrelated ticket's URL, even one the caller is a member of");
        require(!tickets.deleteWorklog("TH-1", webWorklog.id, sam),
               "a worklog cannot be deleted through an unrelated ticket's URL, even one the caller is a member of");
        // The correctly-scoped route still works for someone who does have
        // WEB access.
        require(tickets.deleteWorklog("WEB-1", webWorklog.id, alex),
               "the same worklog can be deleted once addressed through its own ticket's URL");
    }

    // --- Attachments (Phase 5, D15/D98-D105) ---
    {
        require(throwsForbidden([&] { tickets.uploadAttachment("WEB-1", "notes.txt", "text/plain", "hello world", sam); }),
               "a non-member cannot upload an attachment to another project's ticket");

        const auto uploaded = tickets.uploadAttachment("TH-1", "notes.txt", "text/plain", "hello world", sam);
        require(uploaded.fileName == "notes.txt" && uploaded.byteSize == 11, "a TH member can upload to a TH ticket");
        require(!uploaded.sha256.empty(), "the uploaded file's SHA-256 is computed and stored (D105)");

        const auto listed = tickets.listAttachments("TH-1", demo);
        require(std::any_of(listed.begin(), listed.end(), [&](const auto& a) { return a.id == uploaded.id; }),
               "the uploaded attachment appears in the ticket's list");

        const auto [downloadedMeta, downloadedBytes] = tickets.downloadAttachment(uploaded.id, demo);
        require(downloadedMeta.id == uploaded.id && downloadedBytes == "hello world",
               "downloadAttachment returns the exact bytes that were uploaded");

        bool unknownAttachmentRejected = false;
        try {
            tickets.downloadAttachment("00000000-0000-4000-8000-00000000dead", demo);
        } catch (const std::invalid_argument&) {
            unknownAttachmentRejected = true;
        }
        require(unknownAttachmentRejected, "downloading an unknown attachment is rejected");

        // Uploader-or-project-Admin-or-above (mirrors D83's comment rule --
        // no decision text addresses attachment deletion directly).
        require(throwsForbidden([&] { tickets.deleteAttachment("TH-1", uploaded.id, alex); }),
               "a non-uploader, non-admin project member cannot delete someone else's attachment");
        require(tickets.deleteAttachment("TH-1", uploaded.id, sam), "the uploader can always delete their own attachment");
        const auto attachmentsAfterDelete = tickets.listAttachments("TH-1", demo);
        require(std::none_of(attachmentsAfterDelete.begin(), attachmentsAfterDelete.end(),
                             [&](const auto& a) { return a.id == uploaded.id; }),
               "a soft-deleted attachment no longer appears in the ticket's list");

        const auto secondUpload = tickets.uploadAttachment("TH-1", "diagram.png", "image/png", "not-really-a-png", alex);
        require(throwsForbidden([&] { tickets.deleteAttachment("TH-1", secondUpload.id, sam); }),
               "a non-uploader TH member (not project-admin) cannot delete another member's attachment");
        require(tickets.deleteAttachment("TH-1", secondUpload.id, demo),
               "the global administrator can delete any attachment");

        // Regression test for the same class of IDOR as comments/worklogs
        // above: deleteAttachment checked the project-role from the URL's
        // ticketKey but looked the attachment up purely by id. Alex is a
        // plain TH member (not TH admin) but *is* WEB admin, so alex must
        // not be able to reach and delete a TH attachment they didn't
        // upload by routing the request through a WEB ticket URL (where
        // alex holds Admin rank, satisfying the role check for the wrong
        // project).
        const auto thAttachmentBySam = tickets.uploadAttachment("TH-1", "cross-project.txt", "text/plain", "sam's file", sam);
        require(!tickets.deleteAttachment("WEB-1", thAttachmentBySam.id, alex),
               "a TH attachment cannot be deleted through a WEB ticket URL (returns false, not Forbidden -- "
               "the attachment/ticket mismatch is caught before the role check even runs), even by a WEB "
               "admin who is only a plain TH member");
        // The correctly-scoped route still rejects alex (not TH admin, not the uploader)...
        require(throwsForbidden([&] { tickets.deleteAttachment("TH-1", thAttachmentBySam.id, alex); }),
               "...and the attachment is still protected when addressed through its own (TH) ticket URL");
        // ...but the uploader can still delete it themselves.
        require(tickets.deleteAttachment("TH-1", thAttachmentBySam.id, sam),
               "the uploader can still delete their own attachment through the correct ticket URL");

        // --- Fixed limits (D98): oversized file and blocked extension ---
        bool oversizedRejected = false;
        try {
            tickets.uploadAttachment("TH-1", "huge.bin", "application/octet-stream",
                                     std::string(TicketHub::Domain::AttachmentMaxBytes + 1, 'x'), alex);
        } catch (const std::invalid_argument&) {
            oversizedRejected = true;
        }
        require(oversizedRejected, "a file exceeding the fixed 25MB limit is rejected");

        bool blockedExtensionRejected = false;
        try {
            tickets.uploadAttachment("TH-1", "malware.exe", "application/octet-stream", "MZ", alex);
        } catch (const std::invalid_argument&) {
            blockedExtensionRejected = true;
        }
        require(blockedExtensionRejected, "a blocked/dangerous file extension is rejected");

        // --- Recycle bin: global-administrator-only (D101/D102) ---
        const auto thirdUpload = tickets.uploadAttachment("TH-1", "keep.txt", "text/plain", "keep me", alex);
        require(tickets.deleteAttachment("TH-1", thirdUpload.id, alex), "soft-delete for the recycle bin test");

        require(throwsForbidden([&] { tickets.listDeletedAttachments(sam); }),
               "listing the attachment recycle bin is global-administrator-only");
        require(throwsForbidden([&] { tickets.restoreAttachment(thirdUpload.id, alex); }),
               "restoring an attachment is global-administrator-only");
        require(throwsForbidden([&] { tickets.permanentlyDeleteAttachment(thirdUpload.id, alex); }),
               "permanently deleting an attachment is global-administrator-only");

        const auto deletedList = tickets.listDeletedAttachments(demo);
        require(std::any_of(deletedList.begin(), deletedList.end(), [&](const auto& a) { return a.id == thirdUpload.id; }),
               "the global administrator sees the soft-deleted attachment in the recycle bin");

        require(tickets.restoreAttachment(thirdUpload.id, demo), "the global administrator can restore an attachment");
        const auto attachmentsAfterRestore = tickets.listAttachments("TH-1", demo);
        require(std::any_of(attachmentsAfterRestore.begin(), attachmentsAfterRestore.end(),
                            [&](const auto& a) { return a.id == thirdUpload.id; }),
               "a restored attachment reappears in the ticket's list");

        require(tickets.deleteAttachment("TH-1", thirdUpload.id, alex), "re-deleted for the permanent-delete test");
        require(tickets.permanentlyDeleteAttachment(thirdUpload.id, demo),
               "the global administrator can permanently delete an attachment");
        require(!tickets.permanentlyDeleteAttachment(thirdUpload.id, demo),
               "permanently deleting an already-gone attachment returns false");
    }

    // --- Project components (D19, KEEP_FOR_V1): project-Admin-or-above,
    // same level as archiving/deleting a project, no separate "component
    // admin" role. Neither alex nor sam is TH-admin (both are plain TH
    // members); alex is WEB-admin, sam has no WEB membership at all. ---
    {
        using TicketHub::Domain::CreateComponentRequest;
        using TicketHub::Domain::EditComponentRequest;

        CreateComponentRequest thComponent;
        thComponent.projectKey = "TH";
        thComponent.name = "Auth-test component";

        require(throwsForbidden([&] { tickets.createComponent(thComponent, alex); }),
               "a plain TH member (not TH admin) cannot create a TH component");
        require(throwsForbidden([&] { tickets.createComponent(thComponent, sam); }),
               "a plain TH member (not TH admin) cannot create a TH component (sam either)");

        CreateComponentRequest webComponent;
        webComponent.projectKey = "WEB";
        webComponent.name = "Auth-test component";
        require(throwsForbidden([&] { tickets.createComponent(webComponent, sam); }),
               "a non-member cannot create a WEB component");
        const auto createdWebComponent = tickets.createComponent(webComponent, alex);
        require(createdWebComponent.name == "Auth-test component", "the WEB admin can create a WEB component");

        require(tickets.listComponents("WEB", sam).size() == 1,
               "any authenticated user can list a project's components, regardless of membership");

        // Regression test for the same class of IDOR as attachments/comments/
        // worklogs above: editComponent/deleteComponent must scope by
        // (projectKey, componentId) together, not componentId alone. Uses
        // demo (global admin, passes the role check for every project) so
        // the mismatch itself -- not a role rejection -- is what's proven
        // here.
        require(!tickets.editComponent("TH", createdWebComponent.id,
                                       EditComponentRequest{"Should not apply", "", std::nullopt, std::nullopt}, demo)
                     .has_value(),
               "a WEB component cannot be edited through a TH project URL (returns nullopt, not applied), even "
               "for a global administrator who passes the role check for every project");
        require(!tickets.deleteComponent("TH", createdWebComponent.id, demo),
               "a WEB component cannot be deleted through a TH project URL either");

        const auto editedWebComponent = tickets.editComponent("WEB", createdWebComponent.id,
            EditComponentRequest{"Renamed component", "Now with a description", std::string("alex@ticket-hub.local"),
                                 std::nullopt},
            alex);
        require(editedWebComponent.has_value() && editedWebComponent->name == "Renamed component",
               "the WEB admin can edit a WEB component through the correctly-scoped URL");
        require(editedWebComponent->lead.has_value() && editedWebComponent->lead->email == "alex@ticket-hub.local",
               "editComponent sets the lead");

        require(throwsForbidden([&] { tickets.deleteComponent("WEB", createdWebComponent.id, sam); }),
               "a non-member cannot delete a WEB component");
        require(tickets.deleteComponent("WEB", createdWebComponent.id, alex),
               "the WEB admin can delete a WEB component through the correctly-scoped URL");
        require(tickets.listComponents("WEB", demo).empty(), "the deleted component no longer appears in the list");
    }

    // --- User directory for @mention autocomplete (D80) ---
    {
        const auto users = tickets.listUsers(sam);
        require(users.size() >= 3, "listUsers returns at least the three seeded demo users");
        require(std::any_of(users.begin(), users.end(),
                            [](const auto& u) { return u.handle.has_value() && *u.handle == "alex"; }),
               "the seeded demo users have their handles populated");
    }

    // --- Ticket recycle bin: project-admin-vs-global-admin split (D22, mirrors D88) ---
    {
        CreateTicketRequest binRequest;
        binRequest.projectKey = "WEB";
        binRequest.summary = "Recycle bin test ticket";
        const auto ticket = tickets.createTicket(binRequest, alex); // alex is WEB admin

        require(throwsForbidden([&] { tickets.deleteTicket(ticket.key, sam); }),
               "a non-member cannot soft-delete a ticket");
        require(tickets.deleteTicket(ticket.key, alex), "a project admin can soft-delete a ticket");

        require(throwsForbidden([&] { tickets.listDeletedTickets(alex); }),
               "listing the ticket recycle bin is global-administrator-only");
        {
            const auto deleted = tickets.listDeletedTickets(demo);
            const auto match = std::find_if(deleted.begin(), deleted.end(),
                                             [&](const auto& candidate) { return candidate.key == ticket.key; });
            require(match != deleted.end(), "the global administrator sees the deleted ticket in the recycle bin");
        }

        require(throwsForbidden([&] { tickets.restoreTicket(ticket.key, alex); }),
               "restoring a ticket from the recycle bin is global-administrator-only");
        require(tickets.restoreTicket(ticket.key, demo), "the global administrator can restore the ticket");

        require(tickets.deleteTicket(ticket.key, alex), "re-deleting for the permanent-delete test");
        require(throwsForbidden([&] { tickets.permanentlyDeleteTicket(ticket.key, alex); }),
               "permanently deleting a ticket is global-administrator-only");
        require(tickets.permanentlyDeleteTicket(ticket.key, demo),
               "the global administrator can permanently delete the ticket");
    }

    // --- Simple bulk actions apply the same authorization/validation per ticket (D36) ---
    {
        CreateTicketRequest thBulk;
        thBulk.projectKey = "TH";
        thBulk.summary = "Bulk 1";
        const auto bulk1 = tickets.createTicket(thBulk, demo);
        thBulk.summary = "Bulk 2";
        const auto bulk2 = tickets.createTicket(thBulk, demo);

        // Sam is a TH member but not a WEB member; TH-9999 does not exist.
        const std::vector<std::string> mixedKeys = {bulk1.key, bulk2.key, "WEB-1", "TH-9999"};
        const auto assignResult = tickets.bulkAssign(mixedKeys, std::string("alex@ticket-hub.local"), sam);
        require(assignResult.succeeded.size() == 2, "bulk assign succeeds for the two accessible TH tickets");
        require(assignResult.failed.size() == 2, "bulk assign reports the inaccessible and unknown tickets as failed");

        const auto labelResult = tickets.bulkAddLabel({bulk1.key, bulk2.key}, "bulk-tested", demo);
        require(labelResult.succeeded.size() == 2, "bulk label succeeds for both tickets");
        {
            const auto found = tickets.findTicket(bulk1.key, demo);
            require(found.has_value() && !found->labels.empty() && found->labels.front() == "bulk-tested",
                   "the bulk-added label is applied");
        }

        const auto statusResult = tickets.bulkChangeStatus({bulk1.key, bulk2.key}, "in-progress", std::nullopt, demo);
        require(statusResult.succeeded.size() == 2, "bulk status change succeeds for both tickets");

        const auto deleteResult = tickets.bulkDelete({bulk1.key, bulk2.key}, demo);
        require(deleteResult.succeeded.size() == 2, "bulk delete (recycle) succeeds for both tickets");
        require(!tickets.findTicket(bulk1.key, demo).has_value(), "a bulk-deleted ticket is no longer found");
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
        require(!tickets.changeStatus("TH-9999", "done", demo), "changing status of an unknown ticket returns false");
        bool notFoundThrew = false;
        try {
            tickets.addComment("TH-9999", "hello", demo);
        } catch (const std::invalid_argument&) {
            notFoundThrew = true;
        }
        require(notFoundThrew, "commenting on an unknown ticket reports invalid_argument, not Forbidden");
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
        {
            // Unlike the recycle bin (listDeletedProjects, below), an archived
            // project stays viewable by any authenticated reader -- not just a
            // global administrator (D87).
            const auto archived = tickets.listArchivedProjects(alex);
            const auto match = std::find_if(archived.begin(), archived.end(),
                                             [](const auto& project) { return project.key == "QA"; });
            require(match != archived.end() && match->archived,
                   "a non-admin reader sees archived QA via listArchivedProjects, with archived=true");
        }
        require(tickets.setProjectArchived("QA", false, demo), "QA can be unarchived");
        {
            const auto archived = tickets.listArchivedProjects(demo);
            const auto match = std::find_if(archived.begin(), archived.end(),
                                             [](const auto& project) { return project.key == "QA"; });
            require(match == archived.end(), "unarchiving QA removes it from listArchivedProjects");
        }
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

        // --- Changing an active project's key (D91): project-Admin-or-above,
        // same level as archiving/deleting a project. Uses its own
        // throwaway project (not QA) since a vacated key is permanently
        // reserved (D90) -- renaming QA itself here would make "QA"
        // unusable for the rest of this block's assertions below.
        {
            const auto rk = tickets.createProject(CreateProjectRequest{"RK", "Rename-key test", ""}, demo);
            require(rk.key == "RK", "throwaway project for the rename-key test is created");
            executeSql(databasePath, R"SQL(
INSERT INTO project_members(project_id, user_id, role_key)
SELECT id, '00000000-0000-4000-8000-000000000002', 'admin' FROM projects WHERE project_key = 'RK';
)SQL");

            require(throwsForbidden([&] { tickets.changeProjectKey("RK", "RK2", sam); }),
                   "a non-member cannot rename a project's key");

            const auto renamed = tickets.changeProjectKey("RK", "RK2", alex);
            require(renamed.has_value() && renamed->key == "RK2",
                   "a project admin (not global admin) can rename their own project's key");

            bool renameToSelfRejected = false;
            try {
                tickets.changeProjectKey("RK2", "RK2", alex);
            } catch (const std::invalid_argument&) {
                renameToSelfRejected = true;
            }
            require(renameToSelfRejected, "renaming a project key to its own current key is rejected");

            bool renameToCollidingKeyRejected = false;
            try {
                tickets.changeProjectKey("TH", "RK2", demo);
            } catch (const std::invalid_argument&) {
                renameToCollidingKeyRejected = true;
            }
            require(renameToCollidingKeyRejected, "renaming to a key already used by another active project is rejected");

            bool renameToAliasedKeyRejected = false;
            try {
                tickets.changeProjectKey("TH", "RK", demo);
            } catch (const std::invalid_argument&) {
                renameToAliasedKeyRejected = true;
            }
            require(renameToAliasedKeyRejected,
                   "renaming to a key reserved by an existing project_key_aliases entry is rejected");

            require(!tickets.changeProjectKey("NOPE", "RK3", demo).has_value(),
                   "renaming an unknown project key returns nullopt rather than throwing");

            require(tickets.deleteProject("RK2", demo), "the throwaway rename-key project is cleaned up");
        }

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
    // By this point in the file, permanentlyDeleteTicket, setAnonymousReadEnabled
    // (twice), and permanentlyDeleteProject have all already run above.
    {
        require(throwsForbidden([&] { tickets.listAuditEvents(sam); }),
               "reading the audit log is global-administrator-only");

        const auto events = tickets.listAuditEvents(demo);
        require(std::any_of(events.begin(), events.end(),
                            [](const auto& e) { return e.category == "admin" && e.action == "ticket.permanently_deleted"; }),
               "permanentlyDeleteTicket recorded an admin/ticket.permanently_deleted audit event");
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

    // --- Kanban board WIP limits (D32/D33) ---
    {
        require(!tickets.listBoardColumns(sam).empty(), "any authenticated user can read the board columns");

        require(throwsForbidden([&] { tickets.setBoardColumnWipLimit("backlog", 5, alex); }),
               "setting a WIP limit is global-administrator-only");

        tickets.setBoardColumnWipLimit("backlog", 5, demo);
        const auto afterSet = tickets.listBoardColumns(sam);
        const auto backlog = std::find_if(afterSet.begin(), afterSet.end(),
                                          [](const auto& c) { return c.statusKey == "backlog"; });
        require(backlog != afterSet.end() && backlog->wipLimit.has_value() && *backlog->wipLimit == 5,
               "the global administrator's WIP limit change is visible to any reader");

        tickets.setBoardColumnWipLimit("backlog", std::nullopt, demo);

        bool unknownStatusRejected = false;
        try {
            tickets.setBoardColumnWipLimit("not-a-real-status", 1, demo);
        } catch (const std::invalid_argument&) {
            unknownStatusRejected = true;
        }
        require(unknownStatusRejected, "setting a WIP limit on an unknown status key is rejected");
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
        require(anonDashboard.assignedToMe.empty() && anonDashboard.watchedTickets.empty()
                    && anonDashboard.upcomingDeadlines.empty(),
               "an anonymous viewer's dashboard has no personal widgets, even when anonymous read is enabled");

        const auto alexDashboard = tickets.dashboard(alex);
        require(alexDashboard.assignedToMe.size() == 2,
               "alex's assigned-to-me widget excludes the Done-category TH-1, keeping TH-3 and WEB-1");
        require(std::none_of(alexDashboard.assignedToMe.begin(), alexDashboard.assignedToMe.end(),
                             [](const auto& ticket) { return ticket.key == "TH-1"; }),
               "a Done-category ticket does not appear in assigned-to-me");
        require(std::any_of(alexDashboard.assignedToMe.begin(), alexDashboard.assignedToMe.end(),
                            [](const auto& ticket) { return ticket.key == "TH-3"; }),
               "an open ticket assigned to the actor appears in assigned-to-me");
        require(std::none_of(alexDashboard.watchedTickets.begin(), alexDashboard.watchedTickets.end(),
                             [](const auto& ticket) { return ticket.key == "TH-2"; }),
               "alex is not watching TH-2 yet (alex is already watching an earlier notification-test ticket)");

        require(tickets.watchTicket("TH-2", alex), "alex watches TH-2 for the dashboard test");
        const auto alexDashboardAfterWatch = tickets.dashboard(alex);
        require(std::any_of(alexDashboardAfterWatch.watchedTickets.begin(), alexDashboardAfterWatch.watchedTickets.end(),
                            [](const auto& ticket) { return ticket.key == "TH-2"; }),
               "the watched-tickets widget reflects a newly watched ticket");
        require(tickets.unwatchTicket("TH-2", alex), "cleanup: alex unwatches TH-2");
    }

    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    std::cout << "Authorization integration tests passed\n";
    return 0;
}
