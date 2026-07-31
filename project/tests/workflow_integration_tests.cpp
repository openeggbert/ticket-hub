#include "application/TicketService.h"
#include "domain/Errors.h"
#include "infrastructure/database/SqliteDatabase.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
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

template <typename Action>
bool throwsInvalidArgument(Action action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

template <typename Action>
bool throwsWorkflowViolation(Action action) {
    try {
        action();
    } catch (const TicketHub::Domain::WorkflowViolation&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using TicketHub::Application::TicketService;
    using TicketHub::Domain::CreateIssueRequest;
    using TicketHub::Domain::Principal;
    using TicketHub::Infrastructure::Database::SqliteDatabase;

    const Principal demo{"00000000-0000-4000-8000-000000000001", "demo@ticket-hub.local", "Demo User", true};

    const fs::path sourceRoot(TICKETHUB_SOURCE_DIR);
    const fs::path databasePath = fs::temp_directory_path() / "ticket-hub-workflow.db";
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

    auto makeRequest = [](const std::string& type, const std::string& summary) {
        CreateIssueRequest request;
        request.projectKey = "TH";
        request.issueTypeKey = type;
        request.summary = summary;
        request.description = "workflow_integration_tests fixture";
        return request;
    };

    // --- Epic/Sub-task hierarchy enforcement (D64-D66) ---
    {
        auto subTaskWithoutParent = makeRequest("sub-task", "Should be rejected: no parent");
        require(throwsInvalidArgument([&] { tickets.createIssue(subTaskWithoutParent, demo); }),
               "a sub-task without a parent is rejected");

        const auto epic = tickets.createIssue(makeRequest("epic", "Hierarchy epic"), demo);

        auto epicWithParent = makeRequest("epic", "Should be rejected: epic with a parent");
        epicWithParent.parentIssueKey = epic.key;
        require(throwsInvalidArgument([&] { tickets.createIssue(epicWithParent, demo); }),
               "an epic cannot have a parent");

        const auto story = tickets.createIssue(makeRequest("story", "Hierarchy story"), demo);

        auto storyWithStoryParent = makeRequest("story", "Should be rejected: parent is not an epic");
        storyWithStoryParent.parentIssueKey = story.key;
        require(throwsInvalidArgument([&] { tickets.createIssue(storyWithStoryParent, demo); }),
               "a story's parent must be an epic, not another story");

        auto storyUnderEpic = makeRequest("story", "Story under the hierarchy epic");
        storyUnderEpic.parentIssueKey = epic.key;
        const auto linkedStory = tickets.createIssue(storyUnderEpic, demo);
        require(linkedStory.parentIssueKey.has_value() && *linkedStory.parentIssueKey == epic.key,
               "a story can link to an epic in the same project");

        auto subTaskUnderEpic = makeRequest("sub-task", "Should be rejected: parent is an epic");
        subTaskUnderEpic.parentIssueKey = epic.key;
        require(throwsInvalidArgument([&] { tickets.createIssue(subTaskUnderEpic, demo); }),
               "a sub-task's parent must be a story/task/bug, not an epic");

        auto crossProjectSubTask = makeRequest("sub-task", "Should be rejected: cross-project parent");
        crossProjectSubTask.projectKey = "WEB";
        crossProjectSubTask.parentIssueKey = story.key; // story is in TH
        require(throwsInvalidArgument([&] { tickets.createIssue(crossProjectSubTask, demo); }),
               "a parent issue must be in the same project");
    }

    // --- Fixed workflow rules: resolution required/cleared, sub-task gate (D68-D70) ---
    {
        const auto story = tickets.createIssue(makeRequest("story", "Workflow story"), demo);

        require(throwsWorkflowViolation([&] { tickets.changeStatus(story.key, "done", demo); }),
               "transitioning to a Done-category status without a resolution is rejected");
        require(throwsWorkflowViolation([&] {
            tickets.changeStatus(story.key, "done", demo, std::string("not-a-real-resolution"));
        }), "an unknown resolution key is rejected");

        require(tickets.changeStatus(story.key, "done", demo, std::string("fixed")),
               "transitioning to Done with a valid resolution succeeds");
        {
            const auto found = tickets.findIssue(story.key, demo);
            require(found.has_value() && found->status.key == "done", "status is Done");
            require(found->resolution.has_value() && *found->resolution == "fixed", "resolution is stored");
        }

        require(tickets.changeStatus(story.key, "backlog", demo), "reopening does not require a resolution");
        {
            const auto found = tickets.findIssue(story.key, demo);
            require(found.has_value() && found->status.key == "backlog", "status is back to Backlog");
            require(!found->resolution.has_value(), "reopening clears the resolution (D70)");
        }

        auto subTaskRequest = makeRequest("sub-task", "Workflow sub-task");
        subTaskRequest.parentIssueKey = story.key;
        const auto subTask = tickets.createIssue(subTaskRequest, demo);

        require(throwsWorkflowViolation([&] {
            tickets.changeStatus(story.key, "done", demo, std::string("fixed"));
        }), "a parent cannot complete while a sub-task is unfinished (D68)");

        require(tickets.changeStatus(subTask.key, "done", demo, std::string("done")),
               "the sub-task itself can be completed");
        require(tickets.changeStatus(story.key, "done", demo, std::string("fixed")),
               "the parent can now complete once its sub-task is finished");

        require(tickets.changeStatus(story.key, "in-progress", demo),
               "reopening the parent succeeds");
        {
            const auto childAfterReopen = tickets.findIssue(subTask.key, demo);
            require(childAfterReopen.has_value() && childAfterReopen->status.key == "done",
                   "reopening the parent leaves the sub-task's status unchanged (D69)");
        }
    }

    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    std::cout << "Workflow integration tests passed\n";
    return 0;
}
