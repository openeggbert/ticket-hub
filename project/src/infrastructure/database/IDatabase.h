#pragma once

#include "domain/Models.h"

#include <cstdint>
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

    // Backup/restore (Phase 7, D106-D108): offline/maintenance-window use
    // only -- no online consistent-snapshot logic, no manifest, no isolated
    // staging environment for restore. The admin is expected to stop the
    // server before running either; neither method itself verifies that.
    // Writes/reads exactly one database dump file into/from
    // `directory` (`database.sqlite3` or `database.sql`, backend-specific);
    // the CLI's `backup`/`restore` commands separately handle copying the
    // attachments directory alongside it, since that is not a database
    // concern. `restore` does not run pending migrations afterward -- the
    // CLI command does that explicitly as a separate, visible step (D109:
    // "forward migrate older supported backups").
    virtual void backup(const std::string& directory) = 0;
    virtual void restore(const std::string& directory) = 0;

    // --- Identity (Phase 1) ---
    // `passwordHash` is an already-encoded Argon2id hash (see
    // common/PasswordHash.h); the database layer never sees a plaintext
    // password or performs hashing itself.
    virtual Domain::User createUser(const Domain::CreateUserRequest& request,
                                    const std::string& passwordHash) = 0;
    virtual std::optional<Domain::User> findUserByEmail(const std::string& email) = 0;
    virtual std::optional<Domain::User> findUserById(const std::string& userId) = 0;
    // D56/D80: used for @mention resolution and the handle-uniqueness
    // pre-check in AuthService::createUser. `handle` is already normalized
    // (lowercase) by the caller.
    virtual std::optional<Domain::User> findUserByHandle(const std::string& handle) = 0;
    virtual std::vector<Domain::User> listUsers() = 0;
    // Returns nullopt if the user has no local credentials at all (should not
    // happen in V1 -- there is no OIDC -- but keeps the port honest).
    virtual std::optional<std::string> findPasswordHash(const std::string& userId) = 0;

    // Minimal login-attempt tracking (Phase 1). The full configurable
    // lockout policy rides along with REST rate limiting in Phase 6; see
    // docs/REDUCED_SCOPE_ROADMAP.md.
    static constexpr int MaxFailedLoginAttempts = 10;
    virtual void recordFailedLogin(const std::string& userId) = 0;
    virtual void resetFailedLogin(const std::string& userId) = 0;
    virtual bool isLoginLocked(const std::string& userId) = 0;

    // Sessions. `tokenHash` is a SHA-256 hex digest of the random session
    // token (see common/Sha256.h) -- the raw token itself is never stored,
    // only ever returned to the caller once, at creation.
    virtual Domain::Session createSession(const std::string& userId,
                                          const std::string& tokenHash,
                                          const std::string& expiresAtIso8601) = 0;
    virtual std::optional<Domain::Session> findSessionByTokenHash(const std::string& tokenHash) = 0;
    virtual void deleteSession(const std::string& sessionId) = 0;
    // Opportunistic housekeeping call (no background job exists in V1).
    virtual void deleteExpiredSessions() = 0;
    // Active-session list and "sign out everywhere" (Phase 6, D54,
    // resequenced from Phase 1). listSessionsForUser returns every
    // non-expired session, newest first. deleteOtherSessionsForUser deletes
    // every session for userId except keepSessionId (the caller's own
    // current session, so "sign out everywhere" cannot lock the caller out
    // of the request they're making) and returns the number removed.
    virtual std::vector<Domain::Session> listSessionsForUser(const std::string& userId) = 0;
    virtual int deleteOtherSessionsForUser(const std::string& userId, const std::string& keepSessionId) = 0;

    // Personal access tokens (Phase 6, D39/D40). Same tokenHash convention
    // as sessions. findPersonalAccessTokenByHash mirrors
    // findSessionByTokenHash exactly: it returns nullopt for an
    // unknown/expired/revoked token, so a caller need only check for a
    // present value to know the token is currently valid.
    // listPersonalAccessTokens returns every token regardless of
    // expiry/revocation state, newest first, so a user can see (and revoke)
    // tokens that already expired.
    virtual Domain::PersonalAccessToken createPersonalAccessToken(const std::string& userId,
                                                                   const std::string& name,
                                                                   const std::string& tokenHash,
                                                                   const std::string& expiresAtIso8601) = 0;
    virtual std::optional<Domain::PersonalAccessToken> findPersonalAccessTokenByHash(const std::string& tokenHash) = 0;
    virtual std::vector<Domain::PersonalAccessToken> listPersonalAccessTokens(const std::string& userId) = 0;
    virtual bool revokePersonalAccessToken(const std::string& tokenId, const std::string& userId) = 0;
    virtual void touchPersonalAccessTokenLastUsed(const std::string& tokenId) = 0;

    // --- Authorization and project lifecycle (Phase 2) ---
    // nullopt means "no membership row" -- combined with an unrecognized
    // role string, Domain::projectRoleRank(...) treats both as no access.
    virtual std::optional<std::string> findProjectRoleByKey(const std::string& projectKey,
                                                             const std::string& userId) = 0;
    virtual Domain::Project createProject(const Domain::CreateProjectRequest& request,
                                          const std::string& creatorUserId) = 0;
    virtual bool setProjectArchived(const std::string& projectKey, bool archived) = 0;
    // Recycle bin: soft delete / restore / list (which purges anything past
    // the fixed 90-day retention on access -- there is no background job to
    // do this proactively, D89) / permanent delete.
    virtual bool softDeleteProject(const std::string& projectKey, const std::string& actorUserId) = 0;
    virtual bool restoreProject(const std::string& projectKey) = 0;
    virtual std::vector<Domain::Project> listDeletedProjects() = 0;
    virtual bool permanentlyDeleteProject(const std::string& projectKey) = 0;

    // Tiny generic key/value store for the handful of installation-level
    // toggles that survived scope reduction (e.g. anonymous read access,
    // D59). Not a general settings framework -- see
    // docs/REDUCED_SCOPE_DATA_MODEL.md section A.
    virtual std::optional<std::string> getSetting(const std::string& key) = 0;
    virtual void setSetting(const std::string& key, const std::string& value) = 0;

    // --- Issue tracker (existing prototype surface, now principal-driven) ---
    virtual std::vector<Domain::Project> listProjects() = 0;
    // Unpaginated, capped at Domain::DefaultPageSize rows -- used internally
    // (dashboard "recent issues", "assigned to me") and by the CSV export
    // route, which deliberately wants everything matching the filter, not
    // one page of it. `GET /api/v1/issues` uses the paginated overload below
    // instead (D126).
    virtual std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter) = 0;
    // Numbered/offset pagination (D126): `limit`/`offset` are already
    // validated/clamped by the caller (TicketService), not re-validated
    // here. Returns exactly `limit` rows starting at `offset`, same
    // ordering/filtering as the unpaginated overload above.
    virtual std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter, int limit, int offset) = 0;
    // Total row count matching `filter`, ignoring pagination -- pairs with
    // the paginated `listIssues` overload above so a caller can compute
    // `totalPages` and know whether more pages exist.
    virtual std::int64_t countIssues(const Domain::IssueFilter& filter) = 0;
    virtual std::optional<Domain::Issue> findIssueByKey(const std::string& issueKey) = 0;
    // `parentIssueKey` inside `request`, if present, must already have been
    // validated by the caller against the fixed hierarchy rules (D64-D66) --
    // this method only resolves the key to a row and persists the link.
    virtual Domain::Issue createIssue(const Domain::CreateIssueRequest& request,
                                      const std::string& reporterUserId) = 0;
    // The fixed workflow rules (D68-D70) are enforced here, transactionally
    // with the status update, since they depend on the current database
    // state (sibling sub-task statuses, the outgoing status's category) and
    // must not race with a concurrent change:
    //  - a transition to a Done-category status requires `resolution` (one
    //    of Domain::isValidResolution) and throws Domain::WorkflowViolation
    //    if it is missing, or if this issue has any non-deleted child issue
    //    whose status is not Done-category yet;
    //  - a transition away from a Done-category status ("reopening") clears
    //    `resolution` to null regardless of what was passed;
    //  - any other transition leaves `resolution` untouched, and `resolution`
    //    is ignored entirely when the status does not actually change.
    virtual bool changeIssueStatus(const std::string& issueKey,
                                   const std::string& statusKey,
                                   const std::string& actorUserId,
                                   std::optional<std::string> resolution = std::nullopt,
                                   std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    // Full-replacement edit of an issue's standard fields (D129), with the
    // same optimistic-locking contract as changeIssueStatus: a mismatched
    // `expectedVersion` throws Domain::ConcurrencyConflict, and every changed
    // field writes one issue_history row. Returns nullopt if the issue does
    // not exist (or is soft-deleted). Does not touch `issueTypeKey` or
    // `parentIssueKey` -- neither is editable yet.
    virtual std::optional<Domain::Issue> editIssue(const std::string& issueKey,
                                                   const Domain::EditIssueRequest& request,
                                                   const std::string& actorUserId,
                                                   std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    virtual std::vector<Domain::Comment> listComments(const std::string& issueKey) = 0;
    virtual Domain::Comment addComment(const Domain::AddCommentRequest& request,
                                       const std::string& authorUserId) = 0;
    virtual std::optional<Domain::Comment> findCommentById(const std::string& commentId) = 0;
    // --- Comment editing and tombstone delete (Phase 4, D81/D82) ---
    // Same optimistic-locking contract as editIssue: a mismatched
    // `expectedVersion` throws Domain::ConcurrencyConflict; sets `edited_at`
    // to the current time and returns nullopt if the comment does not exist
    // (or is already soft-deleted). Permission (author, or project/global
    // admin, D83) is enforced by TicketService, not here.
    virtual std::optional<Domain::Comment> editComment(const std::string& commentId,
                                                        const std::string& body,
                                                        const std::string& actorUserId,
                                                        std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    // Tombstone delete (D82): sets deleted_at/deleted_by_user_id, same as
    // issues/projects. The comment row and its original body remain in the
    // database (visible to a direct DB query, not through any V1 API) --
    // there is no separate admin recycle-bin UI/API for comments, unlike
    // issues and projects; the existing soft-delete columns are the whole
    // mechanism this decision calls for. Returns false if the comment does
    // not exist or is already deleted.
    virtual bool deleteComment(const std::string& commentId, const std::string& actorUserId) = 0;
    // --- Fixed emoji reactions on comments (Phase 4, D84) ---
    // addCommentReaction/removeCommentReaction return true only if a row was
    // actually inserted/removed (idempotent: reacting twice with the same key,
    // or un-reacting with no existing reaction, is a no-op), matching the
    // watchIssue/voteIssue convention. `reactionKey` must satisfy
    // Domain::isValidCommentReactionKey; enforcing that is the caller's job
    // (TicketService), not this layer's.
    virtual bool addCommentReaction(const std::string& commentId, const std::string& userId,
                                    const std::string& reactionKey) = 0;
    virtual bool removeCommentReaction(const std::string& commentId, const std::string& userId,
                                       const std::string& reactionKey) = 0;
    virtual std::vector<Domain::CommentReaction> listCommentReactions(const std::string& commentId) = 0;

    // --- Fixed in-app notifications (Phase 4, D14/D80) ---
    // `issueId` is the internal issue id, not the display key -- callers
    // already have it from a just-fetched Domain::Issue. There is no
    // dedicated "read" fetch: notifications are always listed for a single
    // user, never looked up individually across users.
    virtual Domain::Notification createNotification(const std::string& userId,
                                                     const std::string& type,
                                                     const std::string& issueId) = 0;
    virtual std::vector<Domain::Notification> listNotifications(const std::string& userId, bool unreadOnly) = 0;
    virtual int countUnreadNotifications(const std::string& userId) = 0;
    // Both return true only if a matching row existed (and, for markRead,
    // was not already read); scoped to `userId` so one user can never mark
    // another's notification read even by guessing an id.
    virtual bool markNotificationRead(const std::string& notificationId, const std::string& userId) = 0;
    virtual bool markAllNotificationsRead(const std::string& userId) = 0;

    // --- Simplified worklogs (Phase 4, D12/D13) ---
    // No remaining-estimate linkage (D12) and no own-vs-others permission
    // split (D13) at this layer either -- TicketService enforces only that
    // the actor has project-Member-or-above on the issue, the same level
    // for add/edit/delete alike. editWorklog shares editComment/editIssue's
    // optimistic-locking contract (`expectedVersion` -> Domain::
    // ConcurrencyConflict). deleteWorklog is a tombstone delete, same
    // mechanism as comments/issues/projects.
    virtual std::vector<Domain::Worklog> listWorklogs(const std::string& issueKey) = 0;
    virtual Domain::Worklog addWorklog(const Domain::AddWorklogRequest& request, const std::string& authorUserId) = 0;
    virtual std::optional<Domain::Worklog> findWorklogById(const std::string& worklogId) = 0;
    virtual std::optional<Domain::Worklog> editWorklog(const std::string& worklogId,
                                                        const Domain::EditWorklogRequest& request,
                                                        std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    virtual bool deleteWorklog(const std::string& worklogId, const std::string& actorUserId) = 0;

    // --- Simple append-only admin/security audit log (Phase 4, D23) ---
    // Fire-and-forget: the caller does not need the created row back, so
    // this returns void rather than reading it back (unlike
    // createNotification, whose result the notification list feature reads
    // immediately). `listAuditEvents` is newest-first, capped by `limit`
    // (there is no pagination, filtering, export, or configurable
    // retention -- rows are simply never purged).
    virtual void recordAuditEvent(const std::string& category,
                                  const std::string& action,
                                  std::optional<std::string> actorUserId,
                                  std::optional<std::string> targetType,
                                  std::optional<std::string> targetId,
                                  std::optional<std::string> details) = 0;
    virtual std::vector<Domain::AuditEvent> listAuditEvents(int limit) = 0;

    virtual Domain::DashboardStats dashboardStats() = 0;

    // --- Kanban board WIP limits (Phase 5, D32/D33) ---
    // A single flat, installation-wide list (D32: one column per fixed
    // workflow status, no per-project boards), ordered by sort_order.
    // setBoardColumnWipLimit returns false for an unknown status key.
    virtual std::vector<Domain::BoardColumn> listBoardColumns() = 0;
    virtual bool setBoardColumnWipLimit(const std::string& statusKey, std::optional<int> wipLimit) = 0;

    // --- Manual ordering (Phase 3, D31) ---
    // Simple integer rank with renumbering, replacing the never-used
    // LexoRank-style string rank. Moves `issueKey` to immediately before
    // `beforeIssueKey` within the same project (both must already be in the
    // same project; throws std::invalid_argument otherwise), or to the end
    // of the project if `beforeIssueKey` is nullopt. Every issue whose
    // rankOrder needs to shift to make room is renumbered by 1 in the same
    // transaction.
    virtual Domain::Issue reorderIssue(const std::string& issueKey,
                                       std::optional<std::string> beforeIssueKey) = 0;

    // --- Move between projects (Phase 3, D37) ---
    // D37: no compatibility check is needed (every project shares the same
    // fixed types/workflow/fields), so a move is just a project_id change
    // plus a new key/number, exactly like creating a fresh issue in the
    // target project. The vacated key becomes a permanent alias (D38) --
    // this is the first code path that actually writes to
    // `issue_key_aliases`, which existed only as a schema foundation before.
    // Rejected (std::invalid_argument) if the issue has a parent or any
    // children, since D64-D66 require a parent and its children to share a
    // project, and re-parenting/un-parenting on move is not implemented.
    virtual Domain::Issue moveIssue(const std::string& issueKey,
                                    const std::string& targetProjectKey,
                                    const std::string& actorUserId) = 0;

    // --- Issue links (Phase 3, D17) ---
    // Rejects an unknown source/target key (std::invalid_argument) and an
    // exact-duplicate (source, target, linkType) triple; a self-link is
    // rejected by the `issues.CHECK(source_issue_id <> target_issue_id)`
    // constraint. The returned view is from the source issue's perspective
    // (`outward = true`).
    virtual Domain::IssueLink createIssueLink(const std::string& sourceIssueKey,
                                              const std::string& targetIssueKey,
                                              const std::string& linkType) = 0;
    // Every link touching `issueKey`, from either end.
    virtual std::vector<Domain::IssueLink> listIssueLinks(const std::string& issueKey) = 0;
    virtual std::optional<Domain::IssueLinkDetail> findIssueLinkById(const std::string& linkId) = 0;
    virtual bool deleteIssueLink(const std::string& linkId) = 0;

    // --- Watchers and voting (Phase 3, D20/D79) ---
    // Self-service only: there is no admin management of another user's
    // watch/vote state, so there is no project-role check either (see
    // TicketService) -- any authenticated user may watch/vote on any issue.
    // watch/voteIssue return true only if the row was newly inserted;
    // unwatch/unvoteIssue return true only if a row was actually removed.
    // Both throw std::invalid_argument for an unknown issue key.
    virtual bool watchIssue(const std::string& issueKey, const std::string& userId) = 0;
    virtual bool unwatchIssue(const std::string& issueKey, const std::string& userId) = 0;
    virtual std::vector<Domain::UserSummary> listWatchers(const std::string& issueKey) = 0;
    // The reverse direction of listWatchers: every non-deleted issue the
    // given user is watching, newest-updated first, capped at `limit`.
    // Backs the personal dashboard's "watched issues" widget (D24).
    virtual std::vector<Domain::Issue> listWatchedIssues(const std::string& userId, int limit) = 0;
    virtual bool voteIssue(const std::string& issueKey, const std::string& userId) = 0;
    virtual bool unvoteIssue(const std::string& issueKey, const std::string& userId) = 0;
    virtual std::vector<Domain::UserSummary> listVoters(const std::string& issueKey) = 0;

    // --- Issue recycle bin (Phase 3, D22) ---
    // Mirrors the project recycle bin (Phase 2, D88/D89): fixed 90-day
    // on-demand retention, no background purge job. `issues.issue_key` keeps
    // its own UNIQUE constraint while soft-deleted, so the key stays
    // reserved; permanent deletion cannot cause key reuse because issue
    // numbers are never reused (a project's next_issue_number only ever
    // increases). `listDeletedIssues` purges anything past 90 days before
    // returning results, same as `listDeletedProjects`.
    virtual bool softDeleteIssue(const std::string& issueKey, const std::string& actorUserId) = 0;
    virtual bool restoreIssue(const std::string& issueKey) = 0;
    virtual std::vector<Domain::Issue> listDeletedIssues() = 0;
    virtual bool permanentlyDeleteIssue(const std::string& issueKey) = 0;

    // --- Attachments (Phase 5, D15/D98-D105) ---
    // IDatabase only ever stores/returns metadata -- it never touches a
    // file. Unlike every other create* method, `createAttachment` takes a
    // caller-supplied `id` rather than generating one internally: the local
    // filesystem storage key must be known (and the file already written)
    // before the row is inserted, so a database row never describes a file
    // that doesn't exist on disk. `id` is also stored as `storage_key`.
    // Attached to an issue, not to an individual comment, so the same
    // attachment can be referenced via `attachment://<id>` from the issue
    // description or from any comment on that issue (D100).
    virtual Domain::Attachment createAttachment(const std::string& id,
                                                const std::string& issueKey,
                                                const std::string& uploaderUserId,
                                                const std::string& fileName,
                                                const std::string& contentType,
                                                std::int64_t byteSize,
                                                const std::string& sha256) = 0;
    // Active (non-deleted) attachments only, oldest first -- D101's
    // "sortable list" is a client-side concern (name/size/date/author/type),
    // not a server-side ordering option.
    virtual std::vector<Domain::Attachment> listAttachments(const std::string& issueKey) = 0;
    virtual std::optional<Domain::Attachment> findAttachmentById(const std::string& attachmentId) = 0;
    // Mirrors the issue/project/comment tombstone pattern exactly (D101).
    virtual bool softDeleteAttachment(const std::string& attachmentId, const std::string& actorUserId) = 0;
    virtual bool restoreAttachment(const std::string& attachmentId) = 0;
    // Unlike `listDeletedIssues`/`listDeletedProjects`, this does NOT purge
    // anything past 90 days itself -- purging an attachment also means
    // deleting its file on disk, which this SQL-only layer cannot do. The
    // fixed 90-day on-demand purge (D102) is implemented one layer up, in
    // TicketService, which has access to both this method and the storage
    // class.
    virtual std::vector<Domain::Attachment> listDeletedAttachments() = 0;
    virtual bool permanentlyDeleteAttachment(const std::string& attachmentId) = 0;
    // Every attachment's storage key under the given issue/project,
    // regardless of the attachment's own soft-delete state -- used to
    // delete files on disk before a permanent issue/project delete cascades
    // through the database (there is no periodic orphan-file audit at all,
    // D105, so this is the only cleanup path for files whose row is about
    // to disappear via ON DELETE CASCADE).
    virtual std::vector<std::string> listAttachmentStorageKeysForIssue(const std::string& issueKey) = 0;
    virtual std::vector<std::string> listAttachmentStorageKeysForProject(const std::string& projectKey) = 0;
};

} // namespace TicketHub::Infrastructure::Database
