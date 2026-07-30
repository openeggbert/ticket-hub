#pragma once

#include "domain/Models.h"

#include <optional>
#include <string>
#include <vector>

namespace TicketHub::Infrastructure::Database {

class IDatabase {
public:
    virtual ~IDatabase() = default;

    virtual std::string backendName() const = 0;
    virtual void migrate() = 0;
    virtual void seedDemoData() = 0;

    virtual std::vector<Domain::Project> listProjects() = 0;
    virtual std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter) = 0;
    virtual std::optional<Domain::Issue> findIssueByKey(const std::string& issueKey) = 0;
    virtual Domain::Issue createIssue(const Domain::CreateIssueRequest& request,
                                      const std::string& reporterUsername) = 0;
    virtual bool changeIssueStatus(const std::string& issueKey,
                                   const std::string& statusKey,
                                   const std::string& actorUsername,
                                   std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    virtual std::vector<Domain::Comment> listComments(const std::string& issueKey) = 0;
    virtual Domain::Comment addComment(const Domain::AddCommentRequest& request,
                                       const std::string& authorUsername) = 0;
    virtual Domain::DashboardStats dashboardStats() = 0;
};

} // namespace TicketHub::Infrastructure::Database
