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
