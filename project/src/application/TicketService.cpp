#include "application/TicketService.h"

#include "domain/Validation.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace TicketHub::Application {

TicketService::TicketService(std::shared_ptr<Infrastructure::Database::IDatabase> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("database must not be null");
    }
}

std::string TicketService::backendName() const {
    return database_->backendName();
}

std::vector<Domain::Project> TicketService::listProjects() {
    return database_->listProjects();
}

std::vector<Domain::Issue> TicketService::listIssues(const Domain::IssueFilter& filter) {
    auto normalized = filter;
    if (normalized.projectKey) {
        normalized.projectKey = Domain::normalizeProjectKey(*normalized.projectKey);
    }
    return database_->listIssues(normalized);
}

std::optional<Domain::Issue> TicketService::findIssue(const std::string& issueKey) {
    return database_->findIssueByKey(Domain::normalizeIssueKey(issueKey));
}

Domain::Issue TicketService::createIssue(Domain::CreateIssueRequest request) {
    request.projectKey = Domain::normalizeProjectKey(request.projectKey);
    for (auto& label : request.labels) {
        label = Domain::normalizeLabel(label);
    }
    std::sort(request.labels.begin(), request.labels.end());
    request.labels.erase(std::unique(request.labels.begin(), request.labels.end()), request.labels.end());
    const auto errors = Domain::validateCreateIssue(request);
    if (!errors.empty()) {
        std::ostringstream message;
        for (std::size_t index = 0; index < errors.size(); ++index) {
            if (index != 0) {
                message << "; ";
            }
            message << errors[index];
        }
        throw std::invalid_argument(message.str());
    }
    return database_->createIssue(request, DemoUser);
}

bool TicketService::changeStatus(const std::string& issueKey,
                                 const std::string& statusKey,
                                 const std::optional<std::int64_t> expectedVersion) {
    if (issueKey.empty() || statusKey.empty()) {
        throw std::invalid_argument("issueKey and statusKey are required");
    }
    return database_->changeIssueStatus(Domain::normalizeIssueKey(issueKey), statusKey, DemoUser, expectedVersion);
}

std::vector<Domain::Comment> TicketService::listComments(const std::string& issueKey) {
    return database_->listComments(Domain::normalizeIssueKey(issueKey));
}

Domain::Comment TicketService::addComment(const std::string& issueKey, const std::string& body) {
    if (body.empty()) {
        throw std::invalid_argument("comment body is required");
    }
    if (body.size() > 100000) {
        throw std::invalid_argument("comment body must not exceed 100000 characters");
    }
    return database_->addComment(Domain::AddCommentRequest{Domain::normalizeIssueKey(issueKey), body}, DemoUser);
}

Domain::DashboardStats TicketService::dashboard() {
    return database_->dashboardStats();
}

} // namespace TicketHub::Application
