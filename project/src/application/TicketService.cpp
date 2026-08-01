#include "application/TicketService.h"

#include "domain/Errors.h"
#include "domain/Validation.h"

#include <algorithm>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace TicketHub::Application {

namespace {
std::string joinErrors(const std::vector<std::string>& errors) {
    std::ostringstream message;
    for (std::size_t index = 0; index < errors.size(); ++index) {
        if (index != 0) {
            message << "; ";
        }
        message << errors[index];
    }
    return message.str();
}

void normalizeLabels(std::vector<std::string>& labels) {
    for (auto& label : labels) {
        label = Domain::normalizeLabel(label);
    }
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
}

void validateCommentBody(const std::string& body) {
    if (body.empty()) {
        throw std::invalid_argument("comment body is required");
    }
    if (body.size() > 100000) {
        throw std::invalid_argument("comment body must not exceed 100000 characters");
    }
}

// @mentions (D80): every distinct `@handle` token in the body, lowercased
// (handles are always stored lowercase, D56), deduplicated. Deliberately
// simple -- no escaping/code-fence awareness, since the comment body isn't
// rendered as Markdown anywhere yet either (D16 is not implemented).
std::set<std::string> extractMentionedHandles(const std::string& body) {
    static const std::regex pattern("@([a-zA-Z0-9_]{1,32})");
    std::set<std::string> handles;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), pattern); it != std::sregex_iterator(); ++it) {
        handles.insert(Domain::normalizeHandle((*it)[1].str()));
    }
    return handles;
}

// Used by the single-field bulk actions (assign, add label): editIssue is a
// full-replacement PUT, so a single-field bulk change still has to carry
// every other current field forward unchanged.
Domain::EditIssueRequest editRequestFrom(const Domain::Issue& issue) {
    Domain::EditIssueRequest request;
    request.summary = issue.summary;
    request.description = issue.description;
    request.priorityKey = issue.priority.key;
    request.assigneeEmail = issue.assignee ? std::optional<std::string>(issue.assignee->email) : std::nullopt;
    request.labels = issue.labels;
    request.storyPoints = issue.storyPoints;
    request.dueDate = issue.dueDate;
    return request;
}
} // namespace

TicketService::TicketService(std::shared_ptr<Infrastructure::Database::IDatabase> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("database must not be null");
    }
}

void TicketService::requireProjectRole(const Domain::Principal& actor,
                                       const std::string& projectKey,
                                       const int minimumRank) const {
    if (actor.isAdmin) {
        return;
    }
    const auto role = database_->findProjectRoleByKey(projectKey, actor.userId);
    const int rank = role ? Domain::projectRoleRank(*role) : -1;
    if (rank < minimumRank) {
        throw Domain::Forbidden("Actor lacks the required role on project " + projectKey);
    }
}

void TicketService::requireGlobalAdmin(const Domain::Principal& actor) const {
    if (!actor.isAdmin) {
        throw Domain::Forbidden("This action requires global administrator privileges");
    }
}

void TicketService::requireReadAccess(const std::optional<Domain::Principal>& actor) {
    if (actor) {
        return;
    }
    if (!isAnonymousReadEnabled()) {
        throw Domain::AuthenticationRequired("Anonymous read access is disabled on this installation");
    }
}

void TicketService::requireValidHierarchy(Domain::CreateIssueRequest& request) {
    const int level = Domain::issueTypeHierarchyLevel(request.issueTypeKey);
    const bool hasParent = request.parentIssueKey.has_value() && !request.parentIssueKey->empty();

    if (level == 1 && hasParent) {
        throw std::invalid_argument("An Epic cannot have a parent issue");
    }
    if (level == -1 && !hasParent) {
        throw std::invalid_argument("A sub-task must have a parent issue");
    }
    if (!hasParent) {
        request.parentIssueKey = std::nullopt;
        return;
    }

    const std::string parentKey = Domain::normalizeIssueKey(*request.parentIssueKey);
    const auto parent = database_->findIssueByKey(parentKey);
    if (!parent) {
        throw std::invalid_argument("Unknown parent issue: " + parentKey);
    }
    if (parent->projectKey != request.projectKey) {
        throw std::invalid_argument("A parent issue must be in the same project");
    }
    const int parentLevel = Domain::issueTypeHierarchyLevel(parent->type.key);
    if (level == -1 && parentLevel != 0) {
        throw std::invalid_argument("A sub-task's parent must be a Story, Task, or Bug");
    }
    if (level == 0 && parentLevel != 1) {
        throw std::invalid_argument("A Story/Task/Bug's parent must be an Epic");
    }
    request.parentIssueKey = parentKey;
}

std::string TicketService::backendName() const {
    return database_->backendName();
}

bool TicketService::isAnonymousReadEnabled() {
    return database_->getSetting("anonymous_read_access") == std::string("true");
}

void TicketService::setAnonymousReadEnabled(const bool enabled, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    database_->setSetting("anonymous_read_access", enabled ? "true" : "false");
    database_->recordAuditEvent("admin", "settings.anonymous_read_changed", actor.userId,
                                std::string("installation_settings"), std::string("anonymous_read_access"),
                                enabled ? std::string("enabled") : std::string("disabled"));
}

std::vector<Domain::Project> TicketService::listProjects(const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listProjects();
}

std::vector<Domain::Issue> TicketService::listIssues(const Domain::IssueFilter& filter,
                                                      const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    auto normalized = filter;
    if (normalized.projectKey) {
        normalized.projectKey = Domain::normalizeProjectKey(*normalized.projectKey);
    }
    return database_->listIssues(normalized);
}

std::optional<Domain::Issue> TicketService::findIssue(const std::string& issueKey,
                                                       const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->findIssueByKey(Domain::normalizeIssueKey(issueKey));
}

Domain::Issue TicketService::createIssue(Domain::CreateIssueRequest request, const Domain::Principal& actor) {
    request.projectKey = Domain::normalizeProjectKey(request.projectKey);
    requireProjectRole(actor, request.projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    requireValidHierarchy(request);
    if (request.assigneeEmail) {
        request.assigneeEmail = Domain::normalizeEmail(*request.assigneeEmail);
    }
    normalizeLabels(request.labels);
    const auto errors = Domain::validateCreateIssue(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    const auto created = database_->createIssue(request, actor.userId);
    dispatchAssignmentNotification(created, std::nullopt, actor);
    return created;
}

bool TicketService::changeStatus(const std::string& issueKey,
                                 const std::string& statusKey,
                                 const Domain::Principal& actor,
                                 const std::optional<std::string> resolution,
                                 const std::optional<std::int64_t> expectedVersion) {
    if (issueKey.empty() || statusKey.empty()) {
        throw std::invalid_argument("issueKey and statusKey are required");
    }
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        return false;
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->changeIssueStatus(normalizedKey, statusKey, actor.userId, resolution, expectedVersion);
}

std::optional<Domain::Issue> TicketService::editIssue(const std::string& issueKey,
                                                       Domain::EditIssueRequest request,
                                                       const Domain::Principal& actor,
                                                       const std::optional<std::int64_t> expectedVersion) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        return std::nullopt;
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    if (request.assigneeEmail) {
        request.assigneeEmail = Domain::normalizeEmail(*request.assigneeEmail);
    }
    normalizeLabels(request.labels);
    const auto errors = Domain::validateEditIssue(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    const auto assigneeBefore = issue->assignee;
    const auto edited = database_->editIssue(normalizedKey, request, actor.userId, expectedVersion);
    if (edited) {
        dispatchAssignmentNotification(*edited, assigneeBefore, actor);
    }
    return edited;
}

std::vector<Domain::Comment> TicketService::listComments(const std::string& issueKey,
                                                          const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listComments(Domain::normalizeIssueKey(issueKey));
}

Domain::Comment TicketService::addComment(const std::string& issueKey, const std::string& body, const Domain::Principal& actor) {
    validateCommentBody(body);
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    const auto comment = database_->addComment(Domain::AddCommentRequest{normalizedKey, body}, actor.userId);
    dispatchCommentNotifications(*issue, comment, actor);
    return comment;
}

std::optional<Domain::Comment> TicketService::editComment(const std::string& issueKey,
                                                           const std::string& commentId,
                                                           const std::string& body,
                                                           const Domain::Principal& actor,
                                                           const std::optional<std::int64_t> expectedVersion) {
    validateCommentBody(body);
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    const auto comment = database_->findCommentById(commentId);
    if (!comment) {
        return std::nullopt;
    }
    if (comment->author.id != actor.userId) {
        requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    }
    return database_->editComment(commentId, body, actor.userId, expectedVersion);
}

bool TicketService::deleteComment(const std::string& issueKey, const std::string& commentId, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    const auto comment = database_->findCommentById(commentId);
    if (!comment) {
        return false;
    }
    if (comment->author.id != actor.userId) {
        requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    }
    return database_->deleteComment(commentId, actor.userId);
}

bool TicketService::addCommentReaction(const std::string& issueKey, const std::string& commentId,
                                       const std::string& reactionKey, const Domain::Principal& actor) {
    if (!Domain::isValidCommentReactionKey(reactionKey)) {
        throw std::invalid_argument("Unknown reaction key: " + reactionKey);
    }
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    if (!database_->findIssueByKey(normalizedKey)) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    if (!database_->findCommentById(commentId)) {
        throw std::invalid_argument("Unknown comment id: " + commentId);
    }
    return database_->addCommentReaction(commentId, actor.userId, reactionKey);
}

bool TicketService::removeCommentReaction(const std::string& issueKey, const std::string& commentId,
                                          const std::string& reactionKey, const Domain::Principal& actor) {
    if (!Domain::isValidCommentReactionKey(reactionKey)) {
        throw std::invalid_argument("Unknown reaction key: " + reactionKey);
    }
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    if (!database_->findIssueByKey(normalizedKey)) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    if (!database_->findCommentById(commentId)) {
        throw std::invalid_argument("Unknown comment id: " + commentId);
    }
    return database_->removeCommentReaction(commentId, actor.userId, reactionKey);
}

std::vector<Domain::CommentReaction> TicketService::listCommentReactions(
    const std::string& issueKey, const std::string& commentId, const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    if (!database_->findIssueByKey(normalizedKey)) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    return database_->listCommentReactions(commentId);
}

std::vector<Domain::Worklog> TicketService::listWorklogs(const std::string& issueKey,
                                                          const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listWorklogs(Domain::normalizeIssueKey(issueKey));
}

Domain::Worklog TicketService::addWorklog(const std::string& issueKey,
                                          const std::string& workDate,
                                          const std::int64_t timeSpentSeconds,
                                          std::optional<std::string> comment,
                                          const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    Domain::AddWorklogRequest request{normalizedKey, workDate, timeSpentSeconds, std::move(comment)};
    const auto errors = Domain::validateAddWorklog(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->addWorklog(request, actor.userId);
}

std::optional<Domain::Worklog> TicketService::editWorklog(const std::string& issueKey,
                                                           const std::string& worklogId,
                                                           const std::string& workDate,
                                                           const std::int64_t timeSpentSeconds,
                                                           std::optional<std::string> comment,
                                                           const Domain::Principal& actor,
                                                           const std::optional<std::int64_t> expectedVersion) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    Domain::EditWorklogRequest request{workDate, timeSpentSeconds, std::move(comment)};
    const auto errors = Domain::validateEditWorklog(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->editWorklog(worklogId, request, expectedVersion);
}

bool TicketService::deleteWorklog(const std::string& issueKey, const std::string& worklogId, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->deleteWorklog(worklogId, actor.userId);
}

Domain::Issue TicketService::cloneIssue(const std::string& issueKey, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto source = database_->findIssueByKey(normalizedKey);
    if (!source) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    requireProjectRole(actor, source->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));

    Domain::CreateIssueRequest clone;
    clone.projectKey = source->projectKey;
    clone.summary = source->summary;
    clone.description = source->description;
    clone.issueTypeKey = source->type.key;
    clone.priorityKey = source->priority.key;
    clone.labels = source->labels;
    if (source->type.key == Domain::IssueTypeSubTask && source->parentIssueKey) {
        clone.parentIssueKey = source->parentIssueKey;
    }

    const auto created = createIssue(clone, actor);
    database_->createIssueLink(created.key, source->key, Domain::LinkTypeClones);
    return created;
}

Domain::Issue TicketService::reorderIssue(const std::string& issueKey,
                                          std::optional<std::string> beforeIssueKey,
                                          const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    if (beforeIssueKey.has_value() && !beforeIssueKey->empty()) {
        beforeIssueKey = Domain::normalizeIssueKey(*beforeIssueKey);
    } else {
        beforeIssueKey = std::nullopt;
    }
    return database_->reorderIssue(normalizedKey, beforeIssueKey);
}

Domain::Issue TicketService::moveIssue(const std::string& issueKey,
                                       const std::string& targetProjectKey,
                                       const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const std::string normalizedTargetProjectKey = Domain::normalizeProjectKey(targetProjectKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    requireProjectRole(actor, normalizedTargetProjectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->moveIssue(normalizedKey, normalizedTargetProjectKey, actor.userId);
}

Domain::IssueLink TicketService::createIssueLink(const std::string& sourceIssueKey,
                                                  const std::string& targetIssueKey,
                                                  const std::string& linkType,
                                                  const Domain::Principal& actor) {
    if (!Domain::isValidLinkType(linkType)) {
        throw std::invalid_argument("Unknown link type: " + linkType);
    }
    const std::string normalizedSource = Domain::normalizeIssueKey(sourceIssueKey);
    const std::string normalizedTarget = Domain::normalizeIssueKey(targetIssueKey);
    if (normalizedSource == normalizedTarget) {
        throw std::invalid_argument("An issue cannot be linked to itself");
    }
    const auto source = database_->findIssueByKey(normalizedSource);
    if (!source) {
        throw std::invalid_argument("Unknown issue key: " + normalizedSource);
    }
    const auto target = database_->findIssueByKey(normalizedTarget);
    if (!target) {
        throw std::invalid_argument("Unknown issue key: " + normalizedTarget);
    }
    requireProjectRole(actor, source->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    requireProjectRole(actor, target->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->createIssueLink(normalizedSource, normalizedTarget, linkType);
}

std::vector<Domain::IssueLink> TicketService::listIssueLinks(const std::string& issueKey,
                                                              const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listIssueLinks(Domain::normalizeIssueKey(issueKey));
}

bool TicketService::deleteIssueLink(const std::string& linkId, const Domain::Principal& actor) {
    const auto link = database_->findIssueLinkById(linkId);
    if (!link) {
        return false;
    }
    requireProjectRole(actor, link->sourceProjectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    requireProjectRole(actor, link->targetProjectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->deleteIssueLink(linkId);
}

bool TicketService::watchIssue(const std::string& issueKey, const Domain::Principal& actor) {
    return database_->watchIssue(Domain::normalizeIssueKey(issueKey), actor.userId);
}

bool TicketService::unwatchIssue(const std::string& issueKey, const Domain::Principal& actor) {
    return database_->unwatchIssue(Domain::normalizeIssueKey(issueKey), actor.userId);
}

std::vector<Domain::UserSummary> TicketService::listWatchers(const std::string& issueKey,
                                                              const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listWatchers(Domain::normalizeIssueKey(issueKey));
}

bool TicketService::voteIssue(const std::string& issueKey, const Domain::Principal& actor) {
    return database_->voteIssue(Domain::normalizeIssueKey(issueKey), actor.userId);
}

bool TicketService::unvoteIssue(const std::string& issueKey, const Domain::Principal& actor) {
    return database_->unvoteIssue(Domain::normalizeIssueKey(issueKey), actor.userId);
}

std::vector<Domain::UserSummary> TicketService::listVoters(const std::string& issueKey,
                                                            const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listVoters(Domain::normalizeIssueKey(issueKey));
}

bool TicketService::deleteIssue(const std::string& issueKey, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        return false;
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    return database_->softDeleteIssue(normalizedKey, actor.userId);
}

bool TicketService::restoreIssue(const std::string& issueKey, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->restoreIssue(Domain::normalizeIssueKey(issueKey));
}

std::vector<Domain::Issue> TicketService::listDeletedIssues(const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->listDeletedIssues();
}

bool TicketService::permanentlyDeleteIssue(const std::string& issueKey, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const bool deleted = database_->permanentlyDeleteIssue(normalizedKey);
    if (deleted) {
        database_->recordAuditEvent("admin", "issue.permanently_deleted", actor.userId, std::string("issue"),
                                    normalizedKey, std::nullopt);
    }
    return deleted;
}

Domain::BulkActionResult TicketService::bulkChangeStatus(const std::vector<std::string>& issueKeys,
                                                          const std::string& statusKey,
                                                          const std::optional<std::string> resolution,
                                                          const Domain::Principal& actor) {
    Domain::BulkActionResult result;
    for (const auto& key : issueKeys) {
        try {
            if (changeStatus(key, statusKey, actor, resolution)) {
                result.succeeded.push_back(key);
            } else {
                result.failed.push_back(key);
            }
        } catch (...) {
            result.failed.push_back(key);
        }
    }
    return result;
}

Domain::BulkActionResult TicketService::bulkAssign(const std::vector<std::string>& issueKeys,
                                                    const std::optional<std::string> assigneeEmail,
                                                    const Domain::Principal& actor) {
    Domain::BulkActionResult result;
    for (const auto& key : issueKeys) {
        try {
            const std::string normalizedKey = Domain::normalizeIssueKey(key);
            const auto issue = database_->findIssueByKey(normalizedKey);
            if (!issue) {
                result.failed.push_back(key);
                continue;
            }
            auto edit = editRequestFrom(*issue);
            edit.assigneeEmail = assigneeEmail;
            if (editIssue(normalizedKey, edit, actor).has_value()) {
                result.succeeded.push_back(key);
            } else {
                result.failed.push_back(key);
            }
        } catch (...) {
            result.failed.push_back(key);
        }
    }
    return result;
}

Domain::BulkActionResult TicketService::bulkAddLabel(const std::vector<std::string>& issueKeys,
                                                      const std::string& label,
                                                      const Domain::Principal& actor) {
    Domain::BulkActionResult result;
    const std::string normalizedLabel = Domain::normalizeLabel(label);
    for (const auto& key : issueKeys) {
        try {
            const std::string normalizedKey = Domain::normalizeIssueKey(key);
            const auto issue = database_->findIssueByKey(normalizedKey);
            if (!issue) {
                result.failed.push_back(key);
                continue;
            }
            auto edit = editRequestFrom(*issue);
            if (std::find(edit.labels.begin(), edit.labels.end(), normalizedLabel) == edit.labels.end()) {
                edit.labels.push_back(normalizedLabel);
            }
            if (editIssue(normalizedKey, edit, actor).has_value()) {
                result.succeeded.push_back(key);
            } else {
                result.failed.push_back(key);
            }
        } catch (...) {
            result.failed.push_back(key);
        }
    }
    return result;
}

Domain::BulkActionResult TicketService::bulkDelete(const std::vector<std::string>& issueKeys, const Domain::Principal& actor) {
    Domain::BulkActionResult result;
    for (const auto& key : issueKeys) {
        try {
            if (deleteIssue(key, actor)) {
                result.succeeded.push_back(key);
            } else {
                result.failed.push_back(key);
            }
        } catch (...) {
            result.failed.push_back(key);
        }
    }
    return result;
}

// Fixed personal dashboard (D24): the installation-wide widgets (counts,
// recent activity) come straight from dashboardStats(); the personal
// widgets (assigned to me, watched, upcoming deadlines) are only
// meaningful for an authenticated actor and stay empty for an anonymous
// viewer. `assignedToMe` reuses listIssues' existing assignee filter
// rather than a dedicated query; `upcomingDeadlines` is derived from that
// same result set (open issues with a due date, soonest first) instead of
// a second database round trip.
Domain::DashboardStats TicketService::dashboard(const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    auto stats = database_->dashboardStats();
    if (!actor) {
        return stats;
    }

    Domain::IssueFilter assignedFilter;
    assignedFilter.assigneeEmail = actor->email;
    const auto assigned = database_->listIssues(assignedFilter);

    std::vector<Domain::Issue> openAssigned;
    std::vector<Domain::Issue> deadlines;
    for (const auto& issue : assigned) {
        if (issue.status.category == "done") {
            continue;
        }
        openAssigned.push_back(issue);
        if (issue.dueDate.has_value()) {
            deadlines.push_back(issue);
        }
    }
    if (openAssigned.size() > 8) {
        openAssigned.resize(8);
    }
    std::sort(deadlines.begin(), deadlines.end(), [](const Domain::Issue& a, const Domain::Issue& b) {
        return *a.dueDate < *b.dueDate;
    });
    if (deadlines.size() > 8) {
        deadlines.resize(8);
    }

    stats.assignedToMe = std::move(openAssigned);
    stats.upcomingDeadlines = std::move(deadlines);
    stats.watchedIssues = database_->listWatchedIssues(actor->userId, 8);
    return stats;
}

std::vector<Domain::BoardColumn> TicketService::listBoardColumns(const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listBoardColumns();
}

void TicketService::setBoardColumnWipLimit(const std::string& statusKey, const std::optional<int> wipLimit,
                                           const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    if (!database_->setBoardColumnWipLimit(statusKey, wipLimit)) {
        throw std::invalid_argument("Unknown status key: " + statusKey);
    }
}

Domain::Project TicketService::createProject(Domain::CreateProjectRequest request, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    request.key = Domain::normalizeProjectKey(request.key);
    const auto errors = Domain::validateCreateProject(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->createProject(request, actor.userId);
}

bool TicketService::setProjectArchived(const std::string& projectKey, const bool archived, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    return database_->setProjectArchived(normalizedKey, archived);
}

bool TicketService::deleteProject(const std::string& projectKey, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    return database_->softDeleteProject(normalizedKey, actor.userId);
}

bool TicketService::restoreProject(const std::string& projectKey, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->restoreProject(Domain::normalizeProjectKey(projectKey));
}

std::vector<Domain::Project> TicketService::listDeletedProjects(const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->listDeletedProjects();
}

bool TicketService::permanentlyDeleteProject(const std::string& projectKey, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    const std::string normalizedKey = Domain::normalizeProjectKey(projectKey);
    const bool deleted = database_->permanentlyDeleteProject(normalizedKey);
    if (deleted) {
        database_->recordAuditEvent("admin", "project.permanently_deleted", actor.userId, std::string("project"),
                                    normalizedKey, std::nullopt);
    }
    return deleted;
}

std::vector<Domain::User> TicketService::listUsers(const Domain::Principal& /*actor*/) {
    return database_->listUsers();
}

std::vector<Domain::Notification> TicketService::listNotifications(const Domain::Principal& actor, const bool unreadOnly) {
    return database_->listNotifications(actor.userId, unreadOnly);
}

int TicketService::countUnreadNotifications(const Domain::Principal& actor) {
    return database_->countUnreadNotifications(actor.userId);
}

bool TicketService::markNotificationRead(const std::string& notificationId, const Domain::Principal& actor) {
    return database_->markNotificationRead(notificationId, actor.userId);
}

bool TicketService::markAllNotificationsRead(const Domain::Principal& actor) {
    return database_->markAllNotificationsRead(actor.userId);
}

std::vector<Domain::AuditEvent> TicketService::listAuditEvents(const Domain::Principal& actor, const int limit) {
    requireGlobalAdmin(actor);
    return database_->listAuditEvents(limit);
}

void TicketService::dispatchAssignmentNotification(const Domain::Issue& issueAfter,
                                                    const std::optional<Domain::UserSummary>& assigneeBefore,
                                                    const Domain::Principal& actor) {
    if (!issueAfter.assignee) {
        return;
    }
    if (issueAfter.assignee->id == actor.userId) {
        return; // Assigning to yourself needs no notification.
    }
    if (assigneeBefore && assigneeBefore->id == issueAfter.assignee->id) {
        return; // Unchanged assignee (e.g. re-saving an edit) -- not a new assignment.
    }
    database_->createNotification(issueAfter.assignee->id, Domain::NotificationTypeAssigned, issueAfter.id);
}

void TicketService::dispatchCommentNotifications(const Domain::Issue& issue,
                                                  const Domain::Comment& comment,
                                                  const Domain::Principal& actor) {
    std::set<std::string> notified;

    for (const auto& handle : extractMentionedHandles(comment.body)) {
        const auto mentioned = database_->findUserByHandle(handle);
        if (mentioned && mentioned->id != actor.userId && notified.insert(mentioned->id).second) {
            database_->createNotification(mentioned->id, Domain::NotificationTypeMentioned, issue.id);
        }
    }

    for (const auto& watcher : database_->listWatchers(issue.key)) {
        if (watcher.id != actor.userId && notified.insert(watcher.id).second) {
            database_->createNotification(watcher.id, Domain::NotificationTypeWatchedComment, issue.id);
        }
    }
}

} // namespace TicketHub::Application
