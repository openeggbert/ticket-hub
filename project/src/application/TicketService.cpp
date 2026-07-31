#include "application/TicketService.h"

#include "domain/Errors.h"
#include "domain/Validation.h"

#include <algorithm>
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
    return database_->createIssue(request, actor.userId);
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
    return database_->editIssue(normalizedKey, request, actor.userId, expectedVersion);
}

std::vector<Domain::Comment> TicketService::listComments(const std::string& issueKey,
                                                          const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listComments(Domain::normalizeIssueKey(issueKey));
}

Domain::Comment TicketService::addComment(const std::string& issueKey, const std::string& body, const Domain::Principal& actor) {
    if (body.empty()) {
        throw std::invalid_argument("comment body is required");
    }
    if (body.size() > 100000) {
        throw std::invalid_argument("comment body must not exceed 100000 characters");
    }
    const std::string normalizedKey = Domain::normalizeIssueKey(issueKey);
    const auto issue = database_->findIssueByKey(normalizedKey);
    if (!issue) {
        throw std::invalid_argument("Unknown issue key: " + normalizedKey);
    }
    requireProjectRole(actor, issue->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->addComment(Domain::AddCommentRequest{normalizedKey, body}, actor.userId);
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
    return database_->permanentlyDeleteIssue(Domain::normalizeIssueKey(issueKey));
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

Domain::DashboardStats TicketService::dashboard(const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->dashboardStats();
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
    return database_->permanentlyDeleteProject(Domain::normalizeProjectKey(projectKey));
}

} // namespace TicketHub::Application
