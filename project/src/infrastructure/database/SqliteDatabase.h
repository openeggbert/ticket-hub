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

    Domain::User createUser(const Domain::CreateUserRequest& request,
                            const std::string& passwordHash) override;
    std::optional<Domain::User> findUserByEmail(const std::string& email) override;
    std::optional<Domain::User> findUserById(const std::string& userId) override;
    std::optional<Domain::User> findUserByHandle(const std::string& handle) override;
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

    std::optional<std::string> findProjectRoleByKey(const std::string& projectKey,
                                                     const std::string& userId) override;
    Domain::Project createProject(const Domain::CreateProjectRequest& request,
                                  const std::string& creatorUserId) override;
    bool setProjectArchived(const std::string& projectKey, bool archived) override;
    bool softDeleteProject(const std::string& projectKey, const std::string& actorUserId) override;
    bool restoreProject(const std::string& projectKey) override;
    std::vector<Domain::Project> listDeletedProjects() override;
    bool permanentlyDeleteProject(const std::string& projectKey) override;

    std::optional<std::string> getSetting(const std::string& key) override;
    void setSetting(const std::string& key, const std::string& value) override;

    std::vector<Domain::Project> listProjects() override;
    std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter) override;
    std::optional<Domain::Issue> findIssueByKey(const std::string& issueKey) override;
    Domain::Issue createIssue(const Domain::CreateIssueRequest& request,
                              const std::string& reporterUserId) override;
    bool changeIssueStatus(const std::string& issueKey,
                           const std::string& statusKey,
                           const std::string& actorUserId,
                           std::optional<std::string> resolution = std::nullopt,
                           std::optional<std::int64_t> expectedVersion = std::nullopt) override;
    std::optional<Domain::Issue> editIssue(const std::string& issueKey,
                                           const Domain::EditIssueRequest& request,
                                           const std::string& actorUserId,
                                           std::optional<std::int64_t> expectedVersion = std::nullopt) override;
    std::vector<Domain::Comment> listComments(const std::string& issueKey) override;
    Domain::Comment addComment(const Domain::AddCommentRequest& request,
                               const std::string& authorUserId) override;
    std::optional<Domain::Comment> findCommentById(const std::string& commentId) override;
    std::optional<Domain::Comment> editComment(const std::string& commentId,
                                               const std::string& body,
                                               const std::string& actorUserId,
                                               std::optional<std::int64_t> expectedVersion = std::nullopt) override;
    bool deleteComment(const std::string& commentId, const std::string& actorUserId) override;
    bool addCommentReaction(const std::string& commentId, const std::string& userId,
                            const std::string& reactionKey) override;
    bool removeCommentReaction(const std::string& commentId, const std::string& userId,
                               const std::string& reactionKey) override;
    std::vector<Domain::CommentReaction> listCommentReactions(const std::string& commentId) override;

    Domain::Notification createNotification(const std::string& userId,
                                             const std::string& type,
                                             const std::string& issueId) override;
    std::vector<Domain::Notification> listNotifications(const std::string& userId, bool unreadOnly) override;
    int countUnreadNotifications(const std::string& userId) override;
    bool markNotificationRead(const std::string& notificationId, const std::string& userId) override;
    bool markAllNotificationsRead(const std::string& userId) override;

    Domain::DashboardStats dashboardStats() override;

    Domain::Issue reorderIssue(const std::string& issueKey, std::optional<std::string> beforeIssueKey) override;
    Domain::Issue moveIssue(const std::string& issueKey,
                            const std::string& targetProjectKey,
                            const std::string& actorUserId) override;

    Domain::IssueLink createIssueLink(const std::string& sourceIssueKey,
                                      const std::string& targetIssueKey,
                                      const std::string& linkType) override;
    std::vector<Domain::IssueLink> listIssueLinks(const std::string& issueKey) override;
    std::optional<Domain::IssueLinkDetail> findIssueLinkById(const std::string& linkId) override;
    bool deleteIssueLink(const std::string& linkId) override;

    bool watchIssue(const std::string& issueKey, const std::string& userId) override;
    bool unwatchIssue(const std::string& issueKey, const std::string& userId) override;
    std::vector<Domain::UserSummary> listWatchers(const std::string& issueKey) override;
    bool voteIssue(const std::string& issueKey, const std::string& userId) override;
    bool unvoteIssue(const std::string& issueKey, const std::string& userId) override;
    std::vector<Domain::UserSummary> listVoters(const std::string& issueKey) override;

    bool softDeleteIssue(const std::string& issueKey, const std::string& actorUserId) override;
    bool restoreIssue(const std::string& issueKey) override;
    std::vector<Domain::Issue> listDeletedIssues() override;
    bool permanentlyDeleteIssue(const std::string& issueKey) override;

private:
    sqlite3* database_{};
    std::string migrationsDirectory_;
    std::string seedPath_;
    std::mutex mutex_;

    void executeScript(const std::string& sql);
};

} // namespace TicketHub::Infrastructure::Database
