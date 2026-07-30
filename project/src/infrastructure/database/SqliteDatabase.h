#pragma once

#include "infrastructure/database/IDatabase.h"

#include <mutex>
#include <sqlite3.h>
#include <string>

namespace TicketHub::Infrastructure::Database {

class SqliteDatabase final : public IDatabase {
public:
    SqliteDatabase(std::string databasePath, std::string migrationsDirectory, std::string seedPath);
    ~SqliteDatabase() override;

    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    std::string backendName() const override;
    void migrate() override;
    void seedDemoData() override;
    std::vector<Domain::Project> listProjects() override;
    std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter) override;
    std::optional<Domain::Issue> findIssueByKey(const std::string& issueKey) override;
    Domain::Issue createIssue(const Domain::CreateIssueRequest& request,
                              const std::string& reporterUsername) override;
    bool changeIssueStatus(const std::string& issueKey,
                           const std::string& statusKey,
                           const std::string& actorUsername,
                           std::optional<std::int64_t> expectedVersion = std::nullopt) override;
    std::vector<Domain::Comment> listComments(const std::string& issueKey) override;
    Domain::Comment addComment(const Domain::AddCommentRequest& request,
                               const std::string& authorUsername) override;
    Domain::DashboardStats dashboardStats() override;

private:
    sqlite3* database_{};
    std::string migrationsDirectory_;
    std::string seedPath_;
    std::mutex mutex_;

    void executeScript(const std::string& sql);
};

} // namespace TicketHub::Infrastructure::Database
