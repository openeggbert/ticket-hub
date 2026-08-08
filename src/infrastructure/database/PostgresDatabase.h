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
    void backup(const std::string& directory) override;
    void restore(const std::string& directory) override;

    Domain::User createUser(const Domain::CreateUserRequest& request,
                            const std::string& passwordHash) override;
    std::optional<Domain::User> findUserByEmail(const std::string& email) override;
    std::optional<Domain::User> findUserById(const std::string& userId) override;
    std::optional<Domain::User> findUserByHandle(const std::string& handle) override;
    std::vector<Domain::User> listUsers() override;
    std::optional<std::string> findPasswordHash(const std::string& userId) override;
    void updateUserPreferences(const std::string& userId, const Domain::UpdatePreferencesRequest& request) override;
    bool setUserActive(const std::string& userId, bool active) override;
    bool setUserAdmin(const std::string& userId, bool isAdmin) override;
    bool setPasswordHash(const std::string& userId, const std::string& passwordHash) override;
    int deleteAllSessionsForUser(const std::string& userId) override;
    void recordFailedLogin(const std::string& userId) override;
    void resetFailedLogin(const std::string& userId) override;
    bool isLoginLocked(const std::string& userId) override;

    Domain::Session createSession(const std::string& userId,
                                  const std::string& tokenHash,
                                  const std::string& expiresAtIso8601) override;
    std::optional<Domain::Session> findSessionByTokenHash(const std::string& tokenHash) override;
    void deleteSession(const std::string& sessionId) override;
    void deleteExpiredSessions() override;
    std::vector<Domain::Session> listSessionsForUser(const std::string& userId) override;
    int deleteOtherSessionsForUser(const std::string& userId, const std::string& keepSessionId) override;

    Domain::PersonalAccessToken createPersonalAccessToken(const std::string& userId,
                                                           const std::string& name,
                                                           const std::string& tokenHash,
                                                           const std::string& expiresAtIso8601) override;
    std::optional<Domain::PersonalAccessToken> findPersonalAccessTokenByHash(const std::string& tokenHash) override;
    std::vector<Domain::PersonalAccessToken> listPersonalAccessTokens(const std::string& userId) override;
    bool revokePersonalAccessToken(const std::string& tokenId, const std::string& userId) override;
    void touchPersonalAccessTokenLastUsed(const std::string& tokenId) override;

    std::optional<std::string> findProjectRoleByKey(const std::string& projectKey,
                                                     const std::string& userId) override;
    Domain::Project createProject(const Domain::CreateProjectRequest& request,
                                  const std::string& creatorUserId) override;
    bool setProjectArchived(const std::string& projectKey, bool archived) override;
    std::optional<Domain::Project> changeProjectKey(const std::string& oldKey, const std::string& newKey) override;
    bool softDeleteProject(const std::string& projectKey, const std::string& actorUserId) override;
    bool restoreProject(const std::string& projectKey) override;
    std::vector<Domain::Project> listDeletedProjects() override;
    bool permanentlyDeleteProject(const std::string& projectKey) override;
    std::vector<Domain::Project> listArchivedProjects() override;

    std::optional<std::string> getSetting(const std::string& key) override;
    void setSetting(const std::string& key, const std::string& value) override;

    std::vector<Domain::ProjectComponent> listComponents(const std::string& projectKey) override;
    Domain::ProjectComponent createComponent(const Domain::CreateComponentRequest& request) override;
    std::optional<Domain::ProjectComponent> findComponentById(const std::string& componentId) override;
    std::optional<Domain::ProjectComponent> editComponent(const std::string& componentId,
                                                            const Domain::EditComponentRequest& request) override;
    bool deleteComponent(const std::string& componentId) override;

    std::vector<Domain::CustomFieldDefinition> listCustomFields(const std::string& projectKey) override;
    Domain::CustomFieldDefinition createCustomField(const Domain::CreateCustomFieldRequest& request) override;
    std::optional<Domain::CustomFieldDefinition> findCustomFieldById(const std::string& fieldId) override;
    std::optional<Domain::CustomFieldDefinition> editCustomField(const std::string& fieldId,
                                                                  const Domain::EditCustomFieldRequest& request) override;
    bool deleteCustomField(const std::string& fieldId) override;
    std::vector<Domain::CustomFieldValue> listTicketCustomFieldValues(const std::string& ticketKey) override;

    std::vector<Domain::Project> listProjects() override;
    std::vector<Domain::Ticket> listTickets(const Domain::TicketFilter& filter) override;
    std::vector<Domain::Ticket> listTickets(const Domain::TicketFilter& filter, int limit, int offset) override;
    std::int64_t countTickets(const Domain::TicketFilter& filter) override;
    std::optional<Domain::Ticket> findTicketByKey(const std::string& ticketKey) override;
    Domain::Ticket createTicket(const Domain::CreateTicketRequest& request,
                              const std::string& reporterUserId) override;
    bool changeTicketStatus(const std::string& ticketKey,
                           const std::string& statusKey,
                           const std::string& actorUserId,
                           std::optional<std::string> resolution = std::nullopt,
                           std::optional<std::int64_t> expectedVersion = std::nullopt) override;
    std::optional<Domain::Ticket> editTicket(const std::string& ticketKey,
                                           const Domain::EditTicketRequest& request,
                                           const std::string& actorUserId,
                                           std::optional<std::int64_t> expectedVersion = std::nullopt) override;
    std::vector<Domain::TicketHistoryEntry> listTicketHistory(const std::string& ticketKey) override;
    std::vector<Domain::Comment> listComments(const std::string& ticketKey) override;
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
                                             const std::string& ticketId) override;
    std::vector<Domain::Notification> listNotifications(const std::string& userId, bool unreadOnly) override;
    std::vector<Domain::Notification> listNotifications(const std::string& userId, bool unreadOnly,
                                                          int limit, int offset) override;
    std::int64_t countNotifications(const std::string& userId, bool unreadOnly) override;
    int countUnreadNotifications(const std::string& userId) override;
    bool markNotificationRead(const std::string& notificationId, const std::string& userId) override;
    bool markAllNotificationsRead(const std::string& userId) override;

    std::vector<Domain::Worklog> listWorklogs(const std::string& ticketKey) override;
    Domain::Worklog addWorklog(const Domain::AddWorklogRequest& request, const std::string& authorUserId) override;
    std::optional<Domain::Worklog> findWorklogById(const std::string& worklogId) override;
    std::optional<Domain::Worklog> editWorklog(const std::string& worklogId,
                                               const Domain::EditWorklogRequest& request,
                                               std::optional<std::int64_t> expectedVersion = std::nullopt) override;
    bool deleteWorklog(const std::string& worklogId, const std::string& actorUserId) override;

    void recordAuditEvent(const std::string& category,
                          const std::string& action,
                          std::optional<std::string> actorUserId,
                          std::optional<std::string> targetType,
                          std::optional<std::string> targetId,
                          std::optional<std::string> details) override;
    std::vector<Domain::AuditEvent> listAuditEvents(int limit) override;
    std::vector<Domain::AuditEvent> listAuditEvents(int limit, int offset) override;
    std::int64_t countAuditEvents() override;

    Domain::DashboardStats dashboardStats() override;
    std::vector<Domain::BoardColumn> listBoardColumns() override;
    bool setBoardColumnWipLimit(const std::string& statusKey, std::optional<int> wipLimit) override;

    Domain::Ticket reorderTicket(const std::string& ticketKey, std::optional<std::string> beforeTicketKey) override;
    Domain::Ticket moveTicket(const std::string& ticketKey,
                            const std::string& targetProjectKey,
                            const std::string& actorUserId) override;

    Domain::TicketLink createTicketLink(const std::string& sourceTicketKey,
                                      const std::string& targetTicketKey,
                                      const std::string& linkType) override;
    std::vector<Domain::TicketLink> listTicketLinks(const std::string& ticketKey) override;
    std::optional<Domain::TicketLinkDetail> findTicketLinkById(const std::string& linkId) override;
    bool deleteTicketLink(const std::string& linkId) override;

    bool watchTicket(const std::string& ticketKey, const std::string& userId) override;
    bool unwatchTicket(const std::string& ticketKey, const std::string& userId) override;
    std::vector<Domain::UserSummary> listWatchers(const std::string& ticketKey) override;
    std::vector<Domain::Ticket> listWatchedTickets(const std::string& userId, int limit) override;
    bool voteTicket(const std::string& ticketKey, const std::string& userId) override;
    bool unvoteTicket(const std::string& ticketKey, const std::string& userId) override;
    std::vector<Domain::UserSummary> listVoters(const std::string& ticketKey) override;

    bool softDeleteTicket(const std::string& ticketKey, const std::string& actorUserId) override;
    bool restoreTicket(const std::string& ticketKey) override;
    std::vector<Domain::Ticket> listDeletedTickets() override;
    bool permanentlyDeleteTicket(const std::string& ticketKey) override;

    Domain::Attachment createAttachment(const std::string& id,
                                        const std::string& ticketKey,
                                        const std::string& uploaderUserId,
                                        const std::string& fileName,
                                        const std::string& contentType,
                                        std::int64_t byteSize,
                                        const std::string& sha256) override;
    std::vector<Domain::Attachment> listAttachments(const std::string& ticketKey) override;
    std::optional<Domain::Attachment> findAttachmentById(const std::string& attachmentId) override;
    bool softDeleteAttachment(const std::string& attachmentId, const std::string& actorUserId) override;
    bool restoreAttachment(const std::string& attachmentId) override;
    std::vector<Domain::Attachment> listDeletedAttachments() override;
    bool permanentlyDeleteAttachment(const std::string& attachmentId) override;
    std::vector<std::string> listAttachmentStorageKeysForTicket(const std::string& ticketKey) override;
    std::vector<std::string> listAttachmentStorageKeysForProject(const std::string& projectKey) override;

    std::vector<Domain::WebhookSubscription> listWebhookSubscriptions() override;
    Domain::WebhookSubscription createWebhookSubscription(const Domain::CreateWebhookSubscriptionRequest& request,
                                                            const std::string& createdByUserId) override;
    bool deleteWebhookSubscription(const std::string& subscriptionId) override;
    void createWebhookDelivery(const std::string& subscriptionId, const std::string& eventType,
                               const std::string& payload) override;
    std::vector<Domain::WebhookDelivery> listPendingWebhookDeliveries(int limit) override;
    void recordWebhookDeliveryResult(const std::string& deliveryId, bool success,
                                     const std::optional<std::string>& error) override;

    void createEmailDelivery(const std::string& recipientUserId, const std::string& subject,
                             const std::string& body) override;
    std::vector<Domain::EmailDelivery> listPendingEmailDeliveries(int limit) override;
    void recordEmailDeliveryResult(const std::string& deliveryId, bool success,
                                   const std::optional<std::string>& error) override;
    Domain::OutboxSummary outboxSummary() override;
    std::vector<Domain::OutboxDelivery> listOutboxDeliveries(int limit, int offset) override;
    bool retryOutboxDelivery(const std::string& channel, const std::string& deliveryId) override;

    std::optional<Domain::IdempotencyRecord> findIdempotencyRecord(
        const std::string& userId, const std::string& idempotencyKey) override;
    void recordIdempotencyResult(const std::string& userId, const std::string& idempotencyKey,
                                 const std::string& requestHash, int responseStatus,
                                 const std::string& responseBody) override;

private:
    std::string connectionString_;
    std::string migrationsDirectory_;
    std::string seedPath_;
};

} // namespace TicketHub::Infrastructure::Database
