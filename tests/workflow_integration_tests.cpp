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
    using TicketHub::Domain::CreateTicketRequest;
    using TicketHub::Domain::EditTicketRequest;
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
        CreateTicketRequest request;
        request.projectKey = "TH";
        request.ticketTypeKey = type;
        request.summary = summary;
        request.description = "workflow_integration_tests fixture";
        return request;
    };

    // Carries every current field forward unchanged (mirrors
    // TicketService::editRequestFrom, which is private) so a test only has
    // to override the one or two fields it's actually exercising.
    auto editFrom = [](const TicketHub::Domain::Ticket& ticket) {
        EditTicketRequest request;
        request.summary = ticket.summary;
        request.description = ticket.description;
        request.priorityKey = ticket.priority.key;
        request.assigneeEmail = ticket.assignee ? std::optional<std::string>(ticket.assignee->email) : std::nullopt;
        request.labels = ticket.labels;
        request.storyPoints = ticket.storyPoints;
        request.dueDate = ticket.dueDate;
        request.ticketTypeKey = ticket.type.key;
        request.parentTicketKey = ticket.parentTicketKey;
        return request;
    };

    // --- Epic/Sub-task hierarchy enforcement (D64-D66) ---
    {
        auto subTaskWithoutParent = makeRequest("sub-task", "Should be rejected: no parent");
        require(throwsInvalidArgument([&] { tickets.createTicket(subTaskWithoutParent, demo); }),
               "a sub-task without a parent is rejected");

        const auto epic = tickets.createTicket(makeRequest("epic", "Hierarchy epic"), demo);

        auto epicWithParent = makeRequest("epic", "Should be rejected: epic with a parent");
        epicWithParent.parentTicketKey = epic.key;
        require(throwsInvalidArgument([&] { tickets.createTicket(epicWithParent, demo); }),
               "an epic cannot have a parent");

        const auto story = tickets.createTicket(makeRequest("story", "Hierarchy story"), demo);

        auto storyWithStoryParent = makeRequest("story", "Should be rejected: parent is not an epic");
        storyWithStoryParent.parentTicketKey = story.key;
        require(throwsInvalidArgument([&] { tickets.createTicket(storyWithStoryParent, demo); }),
               "a story's parent must be an epic, not another story");

        auto storyUnderEpic = makeRequest("story", "Story under the hierarchy epic");
        storyUnderEpic.parentTicketKey = epic.key;
        const auto linkedStory = tickets.createTicket(storyUnderEpic, demo);
        require(linkedStory.parentTicketKey.has_value() && *linkedStory.parentTicketKey == epic.key,
               "a story can link to an epic in the same project");

        auto subTaskUnderEpic = makeRequest("sub-task", "Should be rejected: parent is an epic");
        subTaskUnderEpic.parentTicketKey = epic.key;
        require(throwsInvalidArgument([&] { tickets.createTicket(subTaskUnderEpic, demo); }),
               "a sub-task's parent must be a story/task/bug, not an epic");

        auto crossProjectSubTask = makeRequest("sub-task", "Should be rejected: cross-project parent");
        crossProjectSubTask.projectKey = "WEB";
        crossProjectSubTask.parentTicketKey = story.key; // story is in TH
        require(throwsInvalidArgument([&] { tickets.createTicket(crossProjectSubTask, demo); }),
               "a parent ticket must be in the same project");
    }

    // --- Re-typing and re-parenting after creation (editTicket), previously
    // unimplemented -- see NEXT.md's prior history ---
    {
        // Same-level retyping (Story/Task/Bug all share hierarchy level 0)
        // never touches parent/child rules and is always allowed.
        const auto toRetype = tickets.createTicket(makeRequest("story", "Retype: story to bug"), demo);
        auto sameLevelEdit = editFrom(toRetype);
        sameLevelEdit.ticketTypeKey = "bug";
        const auto retyped = tickets.editTicket(toRetype.key, sameLevelEdit, demo);
        require(retyped.has_value() && retyped->type.key == "bug",
               "retyping among Story/Task/Bug (same hierarchy level) succeeds");

        // Re-parenting alone (type unchanged): move a story from one epic
        // to another, then clear its parent entirely.
        const auto epicA = tickets.createTicket(makeRequest("epic", "Reparent: epic A"), demo);
        const auto epicB = tickets.createTicket(makeRequest("epic", "Reparent: epic B"), demo);
        auto storyUnderA = makeRequest("story", "Reparent: story under epic A");
        storyUnderA.parentTicketKey = epicA.key;
        const auto movable = tickets.createTicket(storyUnderA, demo);

        auto reparentEdit = editFrom(movable);
        reparentEdit.parentTicketKey = epicB.key;
        const auto reparented = tickets.editTicket(movable.key, reparentEdit, demo);
        require(reparented.has_value() && reparented->parentTicketKey.has_value()
                    && *reparented->parentTicketKey == epicB.key,
               "re-parenting alone (type unchanged) moves a story to a different epic");

        auto clearParentEdit = editFrom(*reparented);
        clearParentEdit.parentTicketKey = std::nullopt;
        const auto cleared = tickets.editTicket(movable.key, clearParentEdit, demo);
        require(cleared.has_value() && !cleared->parentTicketKey.has_value(), "a parent can be cleared via edit");

        // An epic still cannot gain a parent via edit; a sub-task still
        // cannot lose its parent via edit -- edit-time re-parenting reuses
        // the exact same shape rules as create-time (D5/D29/D64-D66).
        auto epicGetsParentEdit = editFrom(epicA);
        epicGetsParentEdit.parentTicketKey = epicB.key;
        require(throwsInvalidArgument([&] { tickets.editTicket(epicA.key, epicGetsParentEdit, demo); }),
               "an epic still cannot have a parent after edit-time re-parenting");

        auto taskForSubtask = tickets.createTicket(makeRequest("task", "Reparent: task parent for a sub-task"), demo);
        auto subTaskRequest = makeRequest("sub-task", "Reparent: sub-task needing a parent");
        subTaskRequest.parentTicketKey = taskForSubtask.key;
        const auto subTask = tickets.createTicket(subTaskRequest, demo);
        auto subTaskLosesParentEdit = editFrom(subTask);
        subTaskLosesParentEdit.parentTicketKey = std::nullopt;
        require(throwsInvalidArgument([&] { tickets.editTicket(subTask.key, subTaskLosesParentEdit, demo); }),
               "a sub-task still cannot lose its parent via edit");

        // Self-parenting is rejected.
        auto selfParentEdit = editFrom(taskForSubtask);
        selfParentEdit.parentTicketKey = taskForSubtask.key;
        require(throwsInvalidArgument([&] { tickets.editTicket(taskForSubtask.key, selfParentEdit, demo); }),
               "a ticket cannot become its own parent via edit");

        // Cross-project parent is rejected at edit time too.
        auto webTask = tickets.createTicket([&] {
            auto request = makeRequest("task", "Reparent: WEB task");
            request.projectKey = "WEB";
            return request;
        }(),
                                           demo);
        auto crossProjectEdit = editFrom(webTask);
        crossProjectEdit.parentTicketKey = epicA.key; // epicA is in TH
        require(throwsInvalidArgument([&] { tickets.editTicket(webTask.key, crossProjectEdit, demo); }),
               "a parent ticket must be in the same project, enforced at edit time too");

        // Retyping across hierarchy levels succeeds when the ticket currently
        // has neither a parent nor children -- clearing/setting the parent
        // in the very same edit, exactly as create-time does.
        const auto childlessEpic = tickets.createTicket(makeRequest("epic", "Retype: childless epic"), demo);
        auto epicToTaskEdit = editFrom(childlessEpic);
        epicToTaskEdit.ticketTypeKey = "task"; // level 1 -> level 0, parentTicketKey already nullopt
        const auto epicBecameTask = tickets.editTicket(childlessEpic.key, epicToTaskEdit, demo);
        require(epicBecameTask.has_value() && epicBecameTask->type.key == "task",
               "a childless epic can be retyped across hierarchy levels");

        auto taskToSubTaskEdit = editFrom(*epicBecameTask);
        taskToSubTaskEdit.ticketTypeKey = "sub-task"; // level 0 -> level -1
        taskToSubTaskEdit.parentTicketKey = taskForSubtask.key; // must supply a parent in the same edit
        const auto taskBecameSubTask = tickets.editTicket(epicBecameTask->key, taskToSubTaskEdit, demo);
        require(taskBecameSubTask.has_value() && taskBecameSubTask->type.key == "sub-task"
                    && taskBecameSubTask->parentTicketKey.has_value() && *taskBecameSubTask->parentTicketKey == taskForSubtask.key,
               "retyping to sub-task succeeds when a valid parent is supplied in the same edit");

        // Retyping across hierarchy levels is rejected while the ticket has
        // children -- the exact scenario a naive implementation would
        // silently orphan/invalidate.
        const auto epicWithChild = tickets.createTicket(makeRequest("epic", "Retype: epic with a child"), demo);
        auto storyUnderChildEpic = makeRequest("story", "Retype: child of the epic-with-child");
        storyUnderChildEpic.parentTicketKey = epicWithChild.key;
        tickets.createTicket(storyUnderChildEpic, demo);
        auto epicWithChildEdit = editFrom(epicWithChild);
        epicWithChildEdit.ticketTypeKey = "task";
        require(throwsInvalidArgument([&] { tickets.editTicket(epicWithChild.key, epicWithChildEdit, demo); }),
               "retyping across hierarchy levels is rejected while the ticket has child tickets");
        // Same-level retyping is still unaffected by having children.
        auto epicWithChildSameLevelEdit = editFrom(epicWithChild);
        epicWithChildSameLevelEdit.ticketTypeKey = "epic"; // no-op level, but exercises the "has children" path
        require(tickets.editTicket(epicWithChild.key, epicWithChildSameLevelEdit, demo).has_value(),
               "same-level retyping succeeds even when the ticket has children");
    }

    // --- Fixed workflow rules: resolution required/cleared, sub-task gate (D68-D70) ---
    {
        const auto story = tickets.createTicket(makeRequest("story", "Workflow story"), demo);

        require(throwsWorkflowViolation([&] { tickets.changeStatus(story.key, "done", demo); }),
               "transitioning to a Done-category status without a resolution is rejected");
        require(throwsWorkflowViolation([&] {
            tickets.changeStatus(story.key, "done", demo, std::string("not-a-real-resolution"));
        }), "an unknown resolution key is rejected");

        require(tickets.changeStatus(story.key, "done", demo, std::string("fixed")),
               "transitioning to Done with a valid resolution succeeds");
        {
            const auto found = tickets.findTicket(story.key, demo);
            require(found.has_value() && found->status.key == "done", "status is Done");
            require(found->resolution.has_value() && *found->resolution == "fixed", "resolution is stored");
        }

        require(tickets.changeStatus(story.key, "backlog", demo), "reopening does not require a resolution");
        {
            const auto found = tickets.findTicket(story.key, demo);
            require(found.has_value() && found->status.key == "backlog", "status is back to Backlog");
            require(!found->resolution.has_value(), "reopening clears the resolution (D70)");
        }

        auto subTaskRequest = makeRequest("sub-task", "Workflow sub-task");
        subTaskRequest.parentTicketKey = story.key;
        const auto subTask = tickets.createTicket(subTaskRequest, demo);

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
            const auto childAfterReopen = tickets.findTicket(subTask.key, demo);
            require(childAfterReopen.has_value() && childAfterReopen->status.key == "done",
                   "reopening the parent leaves the sub-task's status unchanged (D69)");
        }
    }

    // --- Simple cloning (D60) ---
    {
        auto original = makeRequest("task", "Clone source task");
        original.labels = {"clone-test", "sample"};
        original.priorityKey = "high";
        const auto source = tickets.createTicket(original, demo);

        const auto clone = tickets.cloneTicket(source.key, demo);
        require(clone.key != source.key, "the clone is a new ticket");
        require(clone.summary == source.summary, "summary is copied");
        require(clone.description == source.description, "description is copied");
        require(clone.type.key == source.type.key, "ticket type is copied");
        require(clone.priority.key == source.priority.key, "priority is copied");
        require(clone.labels == source.labels, "labels are copied");
        require(!clone.assignee.has_value(), "assignee is not copied");
        require(!clone.parentTicketKey.has_value(), "a non-sub-task clone has no parent/epic link copied");

        const auto cloneLinks = tickets.listTicketLinks(clone.key, demo);
        require(cloneLinks.size() == 1 && cloneLinks[0].outward
                    && cloneLinks[0].linkType == TicketHub::Domain::LinkTypeClones
                    && cloneLinks[0].otherTicketKey == source.key,
               "the clone has an outward 'clones' link to the original");

        const auto sourceLinks = tickets.listTicketLinks(source.key, demo);
        require(sourceLinks.size() == 1 && !sourceLinks[0].outward && sourceLinks[0].otherTicketKey == clone.key,
               "the original has the inverse 'is cloned by' link");
    }

    // --- Cloning a sub-task retains its (structurally required) parent ---
    {
        const auto parent = tickets.createTicket(makeRequest("task", "Clone parent task"), demo);
        auto subTaskRequest = makeRequest("sub-task", "Clone source sub-task");
        subTaskRequest.parentTicketKey = parent.key;
        const auto subTask = tickets.createTicket(subTaskRequest, demo);

        const auto clonedSubTask = tickets.cloneTicket(subTask.key, demo);
        require(clonedSubTask.parentTicketKey.has_value() && *clonedSubTask.parentTicketKey == parent.key,
               "cloning a sub-task keeps its original parent, since a sub-task cannot exist without one");
    }

    // --- Ticket links: basic lifecycle through TicketService (D17) ---
    {
        const auto a = tickets.createTicket(makeRequest("task", "Link source"), demo);
        const auto b = tickets.createTicket(makeRequest("task", "Link target"), demo);

        require(throwsInvalidArgument([&] {
            tickets.createTicketLink(a.key, a.key, TicketHub::Domain::LinkTypeRelatesTo, demo);
        }), "a ticket cannot be linked to itself");
        require(throwsInvalidArgument([&] {
            tickets.createTicketLink(a.key, b.key, "not-a-real-link-type", demo);
        }), "an unknown link type is rejected");

        const auto link = tickets.createTicketLink(a.key, b.key, TicketHub::Domain::LinkTypeBlocks, demo);
        require(tickets.listTicketLinks(a.key, demo).size() == 1, "the link appears on the source ticket");
        require(tickets.listTicketLinks(b.key, demo).size() == 1, "the link appears on the target ticket");
        require(tickets.deleteTicketLink(link.id, demo), "the link can be deleted");
        require(!tickets.deleteTicketLink(link.id, demo), "deleting an already-gone link returns false");
    }

    fs::remove(databasePath, removeError);
    fs::remove(databasePath.string() + "-wal", removeError);
    fs::remove(databasePath.string() + "-shm", removeError);

    std::cout << "Workflow integration tests passed\n";
    return 0;
}
