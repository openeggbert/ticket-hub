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

    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    std::cout << "Authorization integration tests passed\n";
    return 0;
}
