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
    // Every write use case takes the caller's Principal explicitly -- there
    // is no fixed demo-user fallback. Callers (the web layer, once wired to
    // Crow) are responsible for resolving a Principal from an authenticated
    // session or PAT before calling these.
    Domain::Issue createIssue(Domain::CreateIssueRequest request, const Domain::Principal& actor);
    bool changeStatus(const std::string& issueKey,
                      const std::string& statusKey,
                      const Domain::Principal& actor,
                      std::optional<std::int64_t> expectedVersion = std::nullopt);
    std::vector<Domain::Comment> listComments(const std::string& issueKey);
    Domain::Comment addComment(const std::string& issueKey, const std::string& body, const Domain::Principal& actor);
    Domain::DashboardStats dashboard();

private:
    std::shared_ptr<Infrastructure::Database::IDatabase> database_;
};

} // namespace TicketHub::Application
