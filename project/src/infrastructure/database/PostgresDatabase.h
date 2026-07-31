#pragma once

#include "infrastructure/database/IDatabase.h"

#include <string>

namespace TicketHub::Infrastructure::Database {

class PostgresDatabase final : public IDatabase {
public:
    PostgresDatabase(std::string connectionString, std::string migrationsDirectory, std::string seedPath);

    std::string backendName() const override;
    void migrate() override;
    void seedDemoData() override;

    Domain::User createUser(const Domain::CreateUserRequest& request,
                            const std::string& passwordHash) override;
    std::optional<Domain::User> findUserByEmail(const std::string& email) override;
    std::optional<Domain::User> findUserById(const std::string& userId) override;
    std::vector<Domain::User> listUsers() override;
    std::optional<std::string> findPasswordHash(const std::string& userId) override;
    void recordFailedLogin(const std::string& userId) override;
    void resetFailedLogin(const std::string& userId) override;
    bool isLoginLocked(const std::string& userId) override;

    Domain::Session createSession(const std::string& userId,
                                  const std::string& tokenHash,
                                  const std::string& expiresAtIso8601) override;
    std::optional<Domain::Session> findSessionByTokenHash(const std::string& tokenHash) override;
    void deleteSession(const std::string& sessionId) override;
    void deleteExpiredSessions() override;

    std::vector<Domain::Project> listProjects() override;
    std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter) override;
    std::optional<Domain::Issue> findIssueByKey(const std::string& issueKey) override;
    Domain::Issue createIssue(const Domain::CreateIssueRequest& request,
                              const std::string& reporterUserId) override;
    bool changeIssueStatus(const std::string& issueKey,
                           const std::string& statusKey,
                           const std::string& actorUserId,
                           std::optional<std::int64_t> expectedVersion = std::nullopt) override;
    std::vector<Domain::Comment> listComments(const std::string& issueKey) override;
    Domain::Comment addComment(const Domain::AddCommentRequest& request,
                               const std::string& authorUserId) override;
    Domain::DashboardStats dashboardStats() override;

private:
    std::string connectionString_;
    std::string migrationsDirectory_;
    std::string seedPath_;
};

} // namespace TicketHub::Infrastructure::Database
