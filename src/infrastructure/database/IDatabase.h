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
    // Self-service timezone/clock-format preferences (D45). The caller
    // (TicketService) always passes the authenticated actor's own userId --
    // there is no cross-user preference management.
    virtual void updateUserPreferences(const std::string& userId, const Domain::UpdatePreferencesRequest& request) = 0;

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
    // Changing an active project's key (D91): the old key becomes a
    // permanent alias (project_key_aliases, mirroring ticket_key_aliases'
    // role for moveTicket/D38) and every ticket in the project -- including
    // soft-deleted ones, since a key must stay resolvable forever -- is
    // renamed to the new prefix with the same numeric suffix, its own old
    // key becoming a ticket_key_aliases entry. Throws std::invalid_argument
    // if newKey is already in use by another active project or reserved by
    // an existing project_key_aliases entry. Returns nullopt if oldKey does
    // not resolve to a live project.
    virtual std::optional<Domain::Project> changeProjectKey(const std::string& oldKey, const std::string& newKey) = 0;
    // Recycle bin: soft delete / restore / list (which purges anything past
    // the fixed 90-day retention on access -- there is no background job to
    // do this proactively, D89) / permanent delete.
    virtual bool softDeleteProject(const std::string& projectKey, const std::string& actorUserId) = 0;
    virtual bool restoreProject(const std::string& projectKey) = 0;
    virtual std::vector<Domain::Project> listDeletedProjects() = 0;
    virtual bool permanentlyDeleteProject(const std::string& projectKey) = 0;
    // Archived (but not soft-deleted) projects, with Domain::Project::archived
    // set true on every result -- the counterpart to listProjects(), which
    // excludes them (D87: archiving "leaves active lists" but the project
    // stays viewable/restorable, unlike the recycle bin).
    virtual std::vector<Domain::Project> listArchivedProjects() = 0;

    // Tiny generic key/value store for the handful of installation-level
    // toggles that survived scope reduction (e.g. anonymous read access,
    // D59). Not a general settings framework -- see
    // docs/REDUCED_SCOPE_DATA_MODEL.md section A.
    virtual std::optional<std::string> getSetting(const std::string& key) = 0;
    virtual void setSetting(const std::string& key, const std::string& value) = 0;

    // --- Project components (D19, KEEP_FOR_V1) ---
    // A small table plus one optional ticket field, per the decision text --
    // no recycle bin/soft-delete. createComponent throws std::invalid_argument
    // on an unknown project key or an already-used (project, name) pair (the
    // table's own UNIQUE constraint). editComponent/deleteComponent return
    // nullopt/false for an unknown componentId; deleting a component that is
    // still referenced by tickets clears it there via ON DELETE SET NULL,
    // not a rejection.
    virtual std::vector<Domain::ProjectComponent> listComponents(const std::string& projectKey) = 0;
    virtual Domain::ProjectComponent createComponent(const Domain::CreateComponentRequest& request) = 0;
    virtual std::optional<Domain::ProjectComponent> findComponentById(const std::string& componentId) = 0;
    virtual std::optional<Domain::ProjectComponent> editComponent(const std::string& componentId,
                                                                    const Domain::EditComponentRequest& request) = 0;
    virtual bool deleteComponent(const std::string& componentId) = 0;

    // --- Custom fields (D9, deferred-after-V1, user-requested) ---
    // Same shape as the component methods above: createCustomField throws
    // std::invalid_argument on an unknown project key or an already-used
    // (project, name) pair; editCustomField/deleteCustomField return
    // nullopt/false for an unknown fieldId. listCustomFields is ordered by
    // sort_order. setTicketCustomFieldValues replaces the ticket's entire
    // value set in one transaction (full-replacement, matching editTicket's
    // labels handling) -- an empty `values` vector clears every value.
    // listTicketCustomFieldValues always returns one entry per field
    // defined on the ticket's project (value nullopt if never set), not
    // just the fields that happen to have a stored value.
    virtual std::vector<Domain::CustomFieldDefinition> listCustomFields(const std::string& projectKey) = 0;
    virtual Domain::CustomFieldDefinition createCustomField(const Domain::CreateCustomFieldRequest& request) = 0;
    virtual std::optional<Domain::CustomFieldDefinition> findCustomFieldById(const std::string& fieldId) = 0;
    virtual std::optional<Domain::CustomFieldDefinition> editCustomField(const std::string& fieldId,
                                                                          const Domain::EditCustomFieldRequest& request) = 0;
    virtual bool deleteCustomField(const std::string& fieldId) = 0;
    // Values are set only as part of createTicket/editTicket (via
    // Domain::CreateTicketRequest::customFieldValues / EditTicketRequest::
    // customFieldValues), not through a separate write method here -- each
    // adapter applies them transactionally alongside the rest of the
    // ticket write, exactly like labels.
    virtual std::vector<Domain::CustomFieldValue> listTicketCustomFieldValues(const std::string& ticketKey) = 0;

    // --- Ticket tracker (existing prototype surface, now principal-driven) ---
    virtual std::vector<Domain::Project> listProjects() = 0;
    // Unpaginated, capped at Domain::DefaultPageSize rows -- used internally
    // (dashboard "recent tickets", "assigned to me") and by the CSV export
    // route, which deliberately wants everything matching the filter, not
    // one page of it. `GET /api/v1/tickets` uses the paginated overload below
    // instead (D126).
    virtual std::vector<Domain::Ticket> listTickets(const Domain::TicketFilter& filter) = 0;
    // Numbered/offset pagination (D126): `limit`/`offset` are already
    // validated/clamped by the caller (TicketService), not re-validated
    // here. Returns exactly `limit` rows starting at `offset`, same
    // ordering/filtering as the unpaginated overload above.
    virtual std::vector<Domain::Ticket> listTickets(const Domain::TicketFilter& filter, int limit, int offset) = 0;
    // Total row count matching `filter`, ignoring pagination -- pairs with
    // the paginated `listTickets` overload above so a caller can compute
    // `totalPages` and know whether more pages exist.
    virtual std::int64_t countTickets(const Domain::TicketFilter& filter) = 0;
    virtual std::optional<Domain::Ticket> findTicketByKey(const std::string& ticketKey) = 0;
    // `parentTicketKey` inside `request`, if present, must already have been
    // validated by the caller against the fixed hierarchy rules (D64-D66) --
    // this method only resolves the key to a row and persists the link.
    virtual Domain::Ticket createTicket(const Domain::CreateTicketRequest& request,
                                      const std::string& reporterUserId) = 0;
    // The fixed workflow rules (D68-D70) are enforced here, transactionally
    // with the status update, since they depend on the current database
    // state (sibling sub-task statuses, the outgoing status's category) and
    // must not race with a concurrent change:
    //  - a transition to a Done-category status requires `resolution` (one
    //    of Domain::isValidResolution) and throws Domain::WorkflowViolation
    //    if it is missing, or if this ticket has any non-deleted child ticket
    //    whose status is not Done-category yet;
    //  - a transition away from a Done-category status ("reopening") clears
    //    `resolution` to null regardless of what was passed;
    //  - any other transition leaves `resolution` untouched, and `resolution`
    //    is ignored entirely when the status does not actually change.
    virtual bool changeTicketStatus(const std::string& ticketKey,
                                   const std::string& statusKey,
                                   const std::string& actorUserId,
                                   std::optional<std::string> resolution = std::nullopt,
                                   std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    // Full-replacement edit of a ticket's standard fields (D129), with the
    // same optimistic-locking contract as changeTicketStatus: a mismatched
    // `expectedVersion` throws Domain::ConcurrencyConflict, and every changed
    // field writes one ticket_history row. Returns nullopt if the ticket does
    // not exist (or is soft-deleted). Does not touch `ticketTypeKey` or
    // `parentTicketKey` -- neither is editable yet.
    virtual std::optional<Domain::Ticket> editTicket(const std::string& ticketKey,
                                                   const Domain::EditTicketRequest& request,
                                                   const std::string& actorUserId,
                                                   std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    // Read-only field-change log already written by changeTicketStatus/
    // editTicket/moveTicket (D129/D37) -- newest first, no artificial cap
    // (matches every other per-ticket list here: comments/worklogs/links
    // have none either, and this V1 product has no bot/automation traffic
    // that could make a single ticket's history unusually large).
    virtual std::vector<Domain::TicketHistoryEntry> listTicketHistory(const std::string& ticketKey) = 0;
    virtual std::vector<Domain::Comment> listComments(const std::string& ticketKey) = 0;
    virtual Domain::Comment addComment(const Domain::AddCommentRequest& request,
                                       const std::string& authorUserId) = 0;
    virtual std::optional<Domain::Comment> findCommentById(const std::string& commentId) = 0;
    // --- Comment editing and tombstone delete (Phase 4, D81/D82) ---
    // Same optimistic-locking contract as editTicket: a mismatched
    // `expectedVersion` throws Domain::ConcurrencyConflict; sets `edited_at`
    // to the current time and returns nullopt if the comment does not exist
    // (or is already soft-deleted). Permission (author, or project/global
    // admin, D83) is enforced by TicketService, not here.
    virtual std::optional<Domain::Comment> editComment(const std::string& commentId,
                                                        const std::string& body,
                                                        const std::string& actorUserId,
                                                        std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    // Tombstone delete (D82): sets deleted_at/deleted_by_user_id, same as
    // tickets/projects. The comment row and its original body remain in the
    // database (visible to a direct DB query, not through any V1 API) --
    // there is no separate admin recycle-bin UI/API for comments, unlike
    // tickets and projects; the existing soft-delete columns are the whole
    // mechanism this decision calls for. Returns false if the comment does
    // not exist or is already deleted.
    virtual bool deleteComment(const std::string& commentId, const std::string& actorUserId) = 0;
    // --- Fixed emoji reactions on comments (Phase 4, D84) ---
    // addCommentReaction/removeCommentReaction return true only if a row was
    // actually inserted/removed (idempotent: reacting twice with the same key,
    // or un-reacting with no existing reaction, is a no-op), matching the
    // watchTicket/voteTicket convention. `reactionKey` must satisfy
    // Domain::isValidCommentReactionKey; enforcing that is the caller's job
    // (TicketService), not this layer's.
    virtual bool addCommentReaction(const std::string& commentId, const std::string& userId,
                                    const std::string& reactionKey) = 0;
    virtual bool removeCommentReaction(const std::string& commentId, const std::string& userId,
                                       const std::string& reactionKey) = 0;
    virtual std::vector<Domain::CommentReaction> listCommentReactions(const std::string& commentId) = 0;

    // --- Fixed in-app notifications (Phase 4, D14/D80) ---
    // `ticketId` is the internal ticket id, not the display key -- callers
    // already have it from a just-fetched Domain::Ticket. There is no
    // dedicated "read" fetch: notifications are always listed for a single
    // user, never looked up individually across users.
    virtual Domain::Notification createNotification(const std::string& userId,
                                                     const std::string& type,
                                                     const std::string& ticketId) = 0;
    virtual std::vector<Domain::Notification> listNotifications(const std::string& userId, bool unreadOnly) = 0;
    // Numbered/offset pagination (extends D126 to this per-user list, which
    // -- unlike a per-ticket comment/worklog list -- has no natural upper
    // bound: notifications accumulate for as long as a user's account
    // exists). Additive: the unpaginated overload above is unchanged and
    // still used wherever the full (small, in practice) unread set is
    // needed (e.g. the notification bell panel).
    virtual std::vector<Domain::Notification> listNotifications(const std::string& userId, bool unreadOnly,
                                                                  int limit, int offset) = 0;
    virtual std::int64_t countNotifications(const std::string& userId, bool unreadOnly) = 0;
    virtual int countUnreadNotifications(const std::string& userId) = 0;
    // Both return true only if a matching row existed (and, for markRead,
    // was not already read); scoped to `userId` so one user can never mark
    // another's notification read even by guessing an id.
    virtual bool markNotificationRead(const std::string& notificationId, const std::string& userId) = 0;
    virtual bool markAllNotificationsRead(const std::string& userId) = 0;

    // --- Simplified worklogs (Phase 4, D12/D13) ---
    // No remaining-estimate linkage (D12) and no own-vs-others permission
    // split (D13) at this layer either -- TicketService enforces only that
    // the actor has project-Member-or-above on the ticket, the same level
    // for add/edit/delete alike. editWorklog shares editComment/editTicket's
    // optimistic-locking contract (`expectedVersion` -> Domain::
    // ConcurrencyConflict). deleteWorklog is a tombstone delete, same
    // mechanism as comments/tickets/projects.
    virtual std::vector<Domain::Worklog> listWorklogs(const std::string& ticketKey) = 0;
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
    // Numbered/offset pagination (extends D126): the audit log is
    // installation-wide and append-only-forever, so unlike a single
    // ticket's comment/worklog list it has no natural upper bound either.
    // Additive alongside the capped-only overload above.
    virtual std::vector<Domain::AuditEvent> listAuditEvents(int limit, int offset) = 0;
    virtual std::int64_t countAuditEvents() = 0;

    virtual Domain::DashboardStats dashboardStats() = 0;

    // --- Kanban board WIP limits (Phase 5, D32/D33) ---
    // A single flat, installation-wide list (D32: one column per fixed
    // workflow status, no per-project boards), ordered by sort_order.
    // setBoardColumnWipLimit returns false for an unknown status key.
    virtual std::vector<Domain::BoardColumn> listBoardColumns() = 0;
    virtual bool setBoardColumnWipLimit(const std::string& statusKey, std::optional<int> wipLimit) = 0;

    // --- Manual ordering (Phase 3, D31) ---
    // Simple integer rank with renumbering, replacing the never-used
    // LexoRank-style string rank. Moves `ticketKey` to immediately before
    // `beforeTicketKey` within the same project (both must already be in the
    // same project; throws std::invalid_argument otherwise), or to the end
    // of the project if `beforeTicketKey` is nullopt. Every ticket whose
    // rankOrder needs to shift to make room is renumbered by 1 in the same
    // transaction.
    virtual Domain::Ticket reorderTicket(const std::string& ticketKey,
                                       std::optional<std::string> beforeTicketKey) = 0;

    // --- Move between projects (Phase 3, D37) ---
    // D37: no compatibility check is needed (every project shares the same
    // fixed types/workflow/fields), so a move is just a project_id change
    // plus a new key/number, exactly like creating a fresh ticket in the
    // target project. The vacated key becomes a permanent alias (D38) --
    // this is the first code path that actually writes to
    // `ticket_key_aliases`, which existed only as a schema foundation before.
    // Rejected (std::invalid_argument) if the ticket has a parent or any
    // children, since D64-D66 require a parent and its children to share a
    // project, and re-parenting/un-parenting on move is not implemented.
    virtual Domain::Ticket moveTicket(const std::string& ticketKey,
                                    const std::string& targetProjectKey,
                                    const std::string& actorUserId) = 0;

    // --- Ticket links (Phase 3, D17) ---
    // Rejects an unknown source/target key (std::invalid_argument) and an
    // exact-duplicate (source, target, linkType) triple; a self-link is
    // rejected by the `tickets.CHECK(source_ticket_id <> target_ticket_id)`
    // constraint. The returned view is from the source ticket's perspective
    // (`outward = true`).
    virtual Domain::TicketLink createTicketLink(const std::string& sourceTicketKey,
                                              const std::string& targetTicketKey,
                                              const std::string& linkType) = 0;
    // Every link touching `ticketKey`, from either end.
    virtual std::vector<Domain::TicketLink> listTicketLinks(const std::string& ticketKey) = 0;
    virtual std::optional<Domain::TicketLinkDetail> findTicketLinkById(const std::string& linkId) = 0;
    virtual bool deleteTicketLink(const std::string& linkId) = 0;

    // --- Watchers and voting (Phase 3, D20/D79) ---
    // Self-service only: there is no admin management of another user's
    // watch/vote state, so there is no project-role check either (see
    // TicketService) -- any authenticated user may watch/vote on any ticket.
    // watch/voteTicket return true only if the row was newly inserted;
    // unwatch/unvoteTicket return true only if a row was actually removed.
    // Both throw std::invalid_argument for an unknown ticket key.
    virtual bool watchTicket(const std::string& ticketKey, const std::string& userId) = 0;
    virtual bool unwatchTicket(const std::string& ticketKey, const std::string& userId) = 0;
    virtual std::vector<Domain::UserSummary> listWatchers(const std::string& ticketKey) = 0;
    // The reverse direction of listWatchers: every non-deleted ticket the
    // given user is watching, newest-updated first, capped at `limit`.
    // Backs the personal dashboard's "watched tickets" widget (D24).
    virtual std::vector<Domain::Ticket> listWatchedTickets(const std::string& userId, int limit) = 0;
    virtual bool voteTicket(const std::string& ticketKey, const std::string& userId) = 0;
    virtual bool unvoteTicket(const std::string& ticketKey, const std::string& userId) = 0;
    virtual std::vector<Domain::UserSummary> listVoters(const std::string& ticketKey) = 0;

    // --- Ticket recycle bin (Phase 3, D22) ---
    // Mirrors the project recycle bin (Phase 2, D88/D89): fixed 90-day
    // on-demand retention, no background purge job. `tickets.ticket_key` keeps
    // its own UNIQUE constraint while soft-deleted, so the key stays
    // reserved; permanent deletion cannot cause key reuse because ticket
    // numbers are never reused (a project's next_ticket_number only ever
    // increases). `listDeletedTickets` purges anything past 90 days before
    // returning results, same as `listDeletedProjects`.
    virtual bool softDeleteTicket(const std::string& ticketKey, const std::string& actorUserId) = 0;
    virtual bool restoreTicket(const std::string& ticketKey) = 0;
    virtual std::vector<Domain::Ticket> listDeletedTickets() = 0;
    virtual bool permanentlyDeleteTicket(const std::string& ticketKey) = 0;

    // --- Attachments (Phase 5, D15/D98-D105) ---
    // IDatabase only ever stores/returns metadata -- it never touches a
    // file. Unlike every other create* method, `createAttachment` takes a
    // caller-supplied `id` rather than generating one internally: the local
    // filesystem storage key must be known (and the file already written)
    // before the row is inserted, so a database row never describes a file
    // that doesn't exist on disk. `id` is also stored as `storage_key`.
    // Attached to a ticket, not to an individual comment, so the same
    // attachment can be referenced via `attachment://<id>` from the ticket
    // description or from any comment on that ticket (D100).
    virtual Domain::Attachment createAttachment(const std::string& id,
                                                const std::string& ticketKey,
                                                const std::string& uploaderUserId,
                                                const std::string& fileName,
                                                const std::string& contentType,
                                                std::int64_t byteSize,
                                                const std::string& sha256) = 0;
    // Active (non-deleted) attachments only, oldest first -- D101's
    // "sortable list" is a client-side concern (name/size/date/author/type),
    // not a server-side ordering option.
    virtual std::vector<Domain::Attachment> listAttachments(const std::string& ticketKey) = 0;
    virtual std::optional<Domain::Attachment> findAttachmentById(const std::string& attachmentId) = 0;
    // Mirrors the ticket/project/comment tombstone pattern exactly (D101).
    virtual bool softDeleteAttachment(const std::string& attachmentId, const std::string& actorUserId) = 0;
    virtual bool restoreAttachment(const std::string& attachmentId) = 0;
    // Unlike `listDeletedTickets`/`listDeletedProjects`, this does NOT purge
    // anything past 90 days itself -- purging an attachment also means
    // deleting its file on disk, which this SQL-only layer cannot do. The
    // fixed 90-day on-demand purge (D102) is implemented one layer up, in
    // TicketService, which has access to both this method and the storage
    // class.
    virtual std::vector<Domain::Attachment> listDeletedAttachments() = 0;
    virtual bool permanentlyDeleteAttachment(const std::string& attachmentId) = 0;
    // Every attachment's storage key under the given ticket/project,
    // regardless of the attachment's own soft-delete state -- used to
    // delete files on disk before a permanent ticket/project delete cascades
    // through the database (there is no periodic orphan-file audit at all,
    // D105, so this is the only cleanup path for files whose row is about
    // to disappear via ON DELETE CASCADE).
    virtual std::vector<std::string> listAttachmentStorageKeysForTicket(const std::string& ticketKey) = 0;
    virtual std::vector<std::string> listAttachmentStorageKeysForProject(const std::string& projectKey) = 0;

    // --- Outbound webhooks (D39/D41, deferred-after-V1) ---
    // Global-admin-managed (TicketService), like PATs/audit log. No
    // recycle bin/soft-delete/edit -- deleteWebhookSubscription is a plain
    // hard delete (cascading away any still-pending deliveries for it via
    // ON DELETE CASCADE); there is no editWebhookSubscription, matching
    // this batch's "create + delete only" scope decision (see
    // docs/SCOPE.md).
    virtual std::vector<Domain::WebhookSubscription> listWebhookSubscriptions() = 0;
    virtual Domain::WebhookSubscription createWebhookSubscription(
        const Domain::CreateWebhookSubscriptionRequest& request, const std::string& createdByUserId) = 0;
    virtual bool deleteWebhookSubscription(const std::string& subscriptionId) = 0;
    // Enqueues one durable delivery row; does not itself attempt delivery
    // (see migrations/*/019_outbox_delivery.sql -- only `ticket-hub-cli
    // process-outbox` ever makes the outbound HTTP call).
    virtual void createWebhookDelivery(const std::string& subscriptionId, const std::string& eventType,
                                       const std::string& payload) = 0;
    // Rows whose next_attempt_at has passed, oldest first, capped by
    // `limit` -- read by the CLI's process-outbox command.
    virtual std::vector<Domain::WebhookDelivery> listPendingWebhookDeliveries(int limit) = 0;
    // Records the outcome of one delivery attempt: on success, marks the
    // row "delivered"; on failure, increments attempt_count and either
    // reschedules next_attempt_at (attempt_count < Domain::
    // MaxDeliveryAttempts) or marks the row permanently "failed".
    virtual void recordWebhookDeliveryResult(const std::string& deliveryId, bool success,
                                             const std::optional<std::string>& error) = 0;

    // --- Outbound email (D52, deferred-after-V1) ---
    // Enqueued only from TicketService, at the same three points that
    // already create an in-app notification (D14) -- see
    // TicketService::maybeEnqueueEmail. Same durable-row-now,
    // deliver-later-via-CLI split as webhooks above.
    virtual void createEmailDelivery(const std::string& recipientUserId, const std::string& subject,
                                     const std::string& body) = 0;
    virtual std::vector<Domain::EmailDelivery> listPendingEmailDeliveries(int limit) = 0;
    virtual void recordEmailDeliveryResult(const std::string& deliveryId, bool success,
                                           const std::optional<std::string>& error) = 0;

    // --- REST write idempotency keys (D128, deferred-after-V1, user-requested) ---
    // Scoped per (userId, idempotencyKey), not globally -- two different
    // users coincidentally choosing the same key value never collide. Only
    // successful (2xx) responses are ever stored (see
    // TicketService::recordIdempotencyResult); a lookup miss simply means
    // "proceed normally," not an error. `recordIdempotencyResult` is
    // best-effort: a duplicate-key write (a narrow concurrent-retry race)
    // is silently ignored rather than raised, since the caller's real
    // response has already been computed and returned either way.
    virtual std::optional<Domain::IdempotencyRecord> findIdempotencyRecord(
        const std::string& userId, const std::string& idempotencyKey) = 0;
    virtual void recordIdempotencyResult(const std::string& userId, const std::string& idempotencyKey,
                                         const std::string& requestHash, int responseStatus,
                                         const std::string& responseBody) = 0;
};

} // namespace TicketHub::Infrastructure::Database
