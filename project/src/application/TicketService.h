#pragma once

#include "domain/Models.h"
#include "infrastructure/database/IDatabase.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TicketHub::Application {

class TicketService {
public:
    explicit TicketService(std::shared_ptr<Infrastructure::Database::IDatabase> database);

    std::string backendName() const;
    std::vector<Domain::Project> listProjects();
    std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter);
    std::optional<Domain::Issue> findIssue(const std::string& issueKey);
    Domain::Issue createIssue(Domain::CreateIssueRequest request);
    bool changeStatus(const std::string& issueKey,
                      const std::string& statusKey,
                      std::optional<std::int64_t> expectedVersion = std::nullopt);
    std::vector<Domain::Comment> listComments(const std::string& issueKey);
    Domain::Comment addComment(const std::string& issueKey, const std::string& body);
    Domain::DashboardStats dashboard();

private:
    std::shared_ptr<Infrastructure::Database::IDatabase> database_;
    static constexpr const char* DemoUser = "demo";
};

} // namespace TicketHub::Application
