#pragma once

#include "domain/Models.h"
#include "infrastructure/database/IDatabase.h"
#include "infrastructure/storage/LocalAttachmentStorage.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TicketHub::Application {

class TicketService {
public:
    // `attachmentsRoot` defaults to a relative dev-mode path; production
    // wiring (src/main.cpp) always passes the configured
    // TICKETHUB_ATTACHMENTS_DIR explicitly. Tests that don't exercise
    // attachments can ignore it; ones that do construct with their own
    // throwaway directory.
    explicit TicketService(std::shared_ptr<Infrastructure::Database::IDatabase> database,
                           std::string attachmentsRoot = "./data/attachments");

    std::string backendName() const;

    // Every read use case takes the caller's Principal explicitly, as
    // std::nullopt for an anonymous (unauthenticated) caller. Project
    // visibility does not otherwise depend on membership -- any authenticated
    // user sees all projects (D58); an anonymous caller is let through only if
    // the installation's anonymous read-access toggle is on (D59, off by
    // default), otherwise Domain::AuthenticationRequired is thrown.
    std::vector<Domain::Project> listProjects(const std::optional<Domain::Principal>& actor);
    std::vector<Domain::Ticket> listTickets(const Domain::TicketFilter& filter, const std::optional<Domain::Principal>& actor);
    // Numbered/offset pagination (D126), used by GET /api/v1/tickets. The
    // unpaginated overload above remains for internal/CSV-export use where
    // "everything matching the filter" is the intended semantics.
    Domain::Page<Domain::Ticket> listTicketsPaged(const Domain::TicketFilter& filter, int page, int pageSize,
                                                const std::optional<Domain::Principal>& actor);
    std::optional<Domain::Ticket> findTicket(const std::string& ticketKey, const std::optional<Domain::Principal>& actor);
    std::vector<Domain::Comment> listComments(const std::string& ticketKey, const std::optional<Domain::Principal>& actor);
    Domain::DashboardStats dashboard(const std::optional<Domain::Principal>& actor);

    // Kanban board WIP limits (D32/D33): a single flat, installation-wide
    // list (same read-access rule as projects/tickets). Setting a limit is
    // global-administrator-only, like the anonymous-read toggle -- there is
    // no per-project board admin concept in the reduced-scope model.
    std::vector<Domain::BoardColumn> listBoardColumns(const std::optional<Domain::Principal>& actor);
    void setBoardColumnWipLimit(const std::string& statusKey, std::optional<int> wipLimit,
                                const Domain::Principal& actor);

    bool isAnonymousReadEnabled();
    // Global-administrator-only: it is an installation-wide toggle, not a
    // per-project setting.
    void setAnonymousReadEnabled(bool enabled, const Domain::Principal& actor);

    // In-app admin version banner (Phase 7, D112): "simple ... banner when a
    // newer version is available; no email delivery." No decision text (or
    // any other doc in this repo) specifies how the app would discover the
    // latest available version -- there is no outbound-HTTP-client
    // infrastructure anywhere in this codebase, and adding one (e.g. to poll
    // a GitHub releases API) would be new capability, not implementing an
    // existing decision. The conservative, explicitly-documented choice
    // here: an admin sets the latest version they know about (e.g. after
    // checking a release page themselves); the app compares that against
    // its own compiled-in `TICKETHUB_VERSION` and shows a banner on
    // mismatch. No outbound network calls, matching the rest of V1's
    // offline-friendly self-hosted posture. Global-administrator-only for
    // both read and write, unlike the anonymous-read-access toggle above
    // (whose value ordinary users' UI behavior depends on) -- only an admin
    // ever acts on this, so there is no reason for a non-admin to see it.
    std::optional<std::string> latestKnownVersion(const Domain::Principal& actor);
    void setLatestKnownVersion(const std::string& version, const Domain::Principal& actor);

    // Every write use case takes the caller's Principal explicitly (never
    // optional) -- there is no anonymous write path and no fixed demo-user
    // fallback. Callers (the web layer, once wired to Crow) are responsible
    // for resolving a Principal from an authenticated session or PAT before
    // calling these.
    // Validates the fixed Epic/Sub-task hierarchy rules (D64-D66) against
    // `request.parentTicketKey` before creating the ticket, throwing
    // std::invalid_argument on a violation (same as any other input
    // validation failure -- these are structural rules on the request, not a
    // Domain::WorkflowViolation, which is reserved for rules that depend on
    // a ticket's current, mutable state).
    Domain::Ticket createTicket(Domain::CreateTicketRequest request, const Domain::Principal& actor);
    // `resolution` is required exactly when `statusKey` names a Done-category
    // status, ignored otherwise, and forced to null when leaving a
    // Done-category status (D68-D70); see IDatabase::changeTicketStatus.
    bool changeStatus(const std::string& ticketKey,
                      const std::string& statusKey,
                      const Domain::Principal& actor,
                      std::optional<std::string> resolution = std::nullopt,
                      std::optional<std::int64_t> expectedVersion = std::nullopt);
    // Full-replacement edit of a ticket's standard fields (D129); see
    // Domain::EditTicketRequest for the PUT-style contract and
    // IDatabase::editTicket for the optimistic-locking/history behavior.
    // Returns nullopt if the ticket does not exist.
    std::optional<Domain::Ticket> editTicket(const std::string& ticketKey,
                                           Domain::EditTicketRequest request,
                                           const Domain::Principal& actor,
                                           std::optional<std::int64_t> expectedVersion = std::nullopt);
    Domain::Comment addComment(const std::string& ticketKey, const std::string& body, const Domain::Principal& actor);

    // --- Comment editing and tombstone delete (Phase 4, D81/D82/D83) ---
    // Simplified permissions (D83): the comment's own author may always edit
    // or delete it; otherwise the actor needs project-Admin-or-above on the
    // comment's ticket's project (or global admin) -- there is no separate
    // edit-own/edit-all/delete-own/delete-all permission matrix. Returns
    // nullopt/false if the comment (or its ticket) does not exist.
    std::optional<Domain::Comment> editComment(const std::string& ticketKey,
                                               const std::string& commentId,
                                               const std::string& body,
                                               const Domain::Principal& actor,
                                               std::optional<std::int64_t> expectedVersion = std::nullopt);
    bool deleteComment(const std::string& ticketKey, const std::string& commentId, const Domain::Principal& actor);

    // --- Fixed emoji reactions on comments (Phase 4, D84) ---
    // Self-service only, same reasoning as watch/vote (D20/D79): no
    // project-role check, just an authenticated actor and an existing
    // comment. `reactionKey` must be one of Domain::isValidCommentReactionKey,
    // and both the ticket and the comment must exist, or this throws
    // std::invalid_argument (matching watchTicket's own unknown-ticket
    // behavior, rather than editComment/deleteComment's nullopt/false
    // not-found convention). add/remove return true only when a row was
    // actually inserted/removed -- reacting (or un-reacting) twice with the
    // same key is a no-op, matching watch/vote.
    bool addCommentReaction(const std::string& ticketKey, const std::string& commentId,
                            const std::string& reactionKey, const Domain::Principal& actor);
    bool removeCommentReaction(const std::string& ticketKey, const std::string& commentId,
                               const std::string& reactionKey, const Domain::Principal& actor);
    std::vector<Domain::CommentReaction> listCommentReactions(const std::string& ticketKey,
                                                               const std::string& commentId,
                                                               const std::optional<Domain::Principal>& actor);

    // --- Simplified worklogs (Phase 4, D12/D13) ---
    // No own-vs-others permission split (D13): add/edit/delete all require
    // the same project-Member-or-above level as any other ticket write --
    // any project member may edit or delete any worklog on a ticket they
    // can access, not just the one they logged themselves. edit/delete
    // return nullopt/false if the worklog (or its ticket) does not exist.
    std::vector<Domain::Worklog> listWorklogs(const std::string& ticketKey, const std::optional<Domain::Principal>& actor);
    Domain::Worklog addWorklog(const std::string& ticketKey,
                               const std::string& workDate,
                               std::int64_t timeSpentSeconds,
                               std::optional<std::string> comment,
                               const Domain::Principal& actor);
    std::optional<Domain::Worklog> editWorklog(const std::string& ticketKey,
                                               const std::string& worklogId,
                                               const std::string& workDate,
                                               std::int64_t timeSpentSeconds,
                                               std::optional<std::string> comment,
                                               const Domain::Principal& actor,
                                               std::optional<std::int64_t> expectedVersion = std::nullopt);
    bool deleteWorklog(const std::string& ticketKey, const std::string& worklogId, const Domain::Principal& actor);

    // Attachments (Phase 5, D15/D98-D105). Read access mirrors comments/
    // tickets (any authenticated user, or anonymous if the installation
    // toggle is on). Uploading requires project-Member-or-above on the
    // ticket's project, the same level as every other ticket write. Deleting
    // is uploader-or-project-Admin-or-above -- no decision text specifies
    // this, so it mirrors D83's comment edit/delete rule as the closest
    // precedent (both are "content a specific user added to a ticket").
    // The recycle bin (list deleted/restore/permanent-delete) is
    // global-administrator-only, the same split as the ticket and project
    // recycle bins.
    std::vector<Domain::Attachment> listAttachments(const std::string& ticketKey,
                                                     const std::optional<Domain::Principal>& actor);
    Domain::Attachment uploadAttachment(const std::string& ticketKey,
                                        const std::string& fileName,
                                        const std::string& contentType,
                                        const std::string& bytes,
                                        const Domain::Principal& actor);
    // Returns the attachment's metadata alongside its raw bytes -- the
    // download route needs both (bytes for the body, metadata for the
    // filename/content-type response headers).
    std::pair<Domain::Attachment, std::string> downloadAttachment(const std::string& attachmentId,
                                                                   const std::optional<Domain::Principal>& actor);
    bool deleteAttachment(const std::string& ticketKey, const std::string& attachmentId, const Domain::Principal& actor);
    std::vector<Domain::Attachment> listDeletedAttachments(const Domain::Principal& actor);
    bool restoreAttachment(const std::string& attachmentId, const Domain::Principal& actor);
    bool permanentlyDeleteAttachment(const std::string& attachmentId, const Domain::Principal& actor);

    // Simple field-copy clone (D60): summary/description/type/priority/
    // labels/component into a new ticket in the same project, plus a
    // `clones`/`is cloned by`
    // link back to the original. Assignee, story points, due date, and
    // (except the one structurally-required case below) the parent/Epic link
    // are not copied. Requires project-Member-or-above, same as createTicket.
    // Special case: a Sub-task cannot exist without a parent (D64), so
    // cloning a Sub-task keeps its original parent -- this is a structural
    // requirement for the clone to be valid at all, not "copying the
    // hierarchy" in the sense the decision excludes.
    Domain::Ticket cloneTicket(const std::string& ticketKey, const Domain::Principal& actor);

    // --- Manual ordering (Phase 3, D31) ---
    // Requires project-Member-or-above on the ticket's own project. A
    // `beforeTicketKey` in a different project is rejected by the database
    // layer with std::invalid_argument before any role check on it would be
    // meaningful (reordering is always a single-project operation).
    Domain::Ticket reorderTicket(const std::string& ticketKey,
                               std::optional<std::string> beforeTicketKey,
                               const Domain::Principal& actor);

    // --- Move between projects (Phase 3, D37) ---
    // Requires project-Member-or-above on both the source and target
    // projects, mirroring createTicketLink's two-project-role-check pattern.
    Domain::Ticket moveTicket(const std::string& ticketKey,
                            const std::string& targetProjectKey,
                            const Domain::Principal& actor);

    // --- Ticket links (Phase 3, D17) ---
    // Both ends of a link must be project-Member-or-above for the actor,
    // since a link write touches two tickets that may be in different
    // projects (unlike other ticket writes, which touch exactly one).
    Domain::TicketLink createTicketLink(const std::string& sourceTicketKey,
                                      const std::string& targetTicketKey,
                                      const std::string& linkType,
                                      const Domain::Principal& actor);
    std::vector<Domain::TicketLink> listTicketLinks(const std::string& ticketKey,
                                                   const std::optional<Domain::Principal>& actor);
    // Returns false if the link does not exist.
    bool deleteTicketLink(const std::string& linkId, const Domain::Principal& actor);

    // --- Watchers and voting (Phase 3, D20/D79) ---
    // Self-service only -- no managing other users' watch/vote state, and no
    // project-role check: only the ticket itself needs to exist. Any
    // authenticated user may watch/vote on any ticket (D58: any authenticated
    // user sees all projects; roles gate writes only, and watch/vote are the
    // one exception even to that, since Jira gates them by "browse" access
    // rather than a write-capable role). watch/vote return true only when the
    // row was newly added; unwatch/unvote return true only when a row was
    // actually removed. All throw std::invalid_argument for an unknown ticket.
    bool watchTicket(const std::string& ticketKey, const Domain::Principal& actor);
    bool unwatchTicket(const std::string& ticketKey, const Domain::Principal& actor);
    std::vector<Domain::UserSummary> listWatchers(const std::string& ticketKey,
                                                   const std::optional<Domain::Principal>& actor);
    bool voteTicket(const std::string& ticketKey, const Domain::Principal& actor);
    bool unvoteTicket(const std::string& ticketKey, const Domain::Principal& actor);
    std::vector<Domain::UserSummary> listVoters(const std::string& ticketKey,
                                                 const std::optional<Domain::Principal>& actor);

    // --- Ticket recycle bin (Phase 3, D22) ---
    // Mirrors the project recycle bin (Phase 2): soft-delete requires
    // project-Admin-or-above (like deleteProject); restoring, listing, and
    // permanently deleting are global-administrator-only, the same
    // "admin restore or permanent delete" split used for projects (D88).
    bool deleteTicket(const std::string& ticketKey, const Domain::Principal& actor);
    bool restoreTicket(const std::string& ticketKey, const Domain::Principal& actor);
    std::vector<Domain::Ticket> listDeletedTickets(const Domain::Principal& actor);
    bool permanentlyDeleteTicket(const std::string& ticketKey, const Domain::Principal& actor);

    // --- Simple bulk actions (Phase 3, D36) ---
    // Each ticket key is processed independently through the corresponding
    // single-ticket operation above -- same authorization, same validation,
    // same workflow rules -- so a bulk call is exactly as safe as doing each
    // action one at a time. No cross-project move and no type change in
    // bulk (D36 explicitly excludes both).
    Domain::BulkActionResult bulkChangeStatus(const std::vector<std::string>& ticketKeys,
                                              const std::string& statusKey,
                                              std::optional<std::string> resolution,
                                              const Domain::Principal& actor);
    Domain::BulkActionResult bulkAssign(const std::vector<std::string>& ticketKeys,
                                        std::optional<std::string> assigneeEmail,
                                        const Domain::Principal& actor);
    Domain::BulkActionResult bulkAddLabel(const std::vector<std::string>& ticketKeys,
                                          const std::string& label,
                                          const Domain::Principal& actor);
    Domain::BulkActionResult bulkDelete(const std::vector<std::string>& ticketKeys, const Domain::Principal& actor);

    // --- Project components (D19, KEEP_FOR_V1) ---
    // Read access mirrors projects/tickets (any authenticated user, or
    // anonymous if the installation toggle is on). Create/edit/delete are
    // project-management actions, so they require project-Admin-or-above,
    // the same level as archiving/deleting a project -- there is no
    // separate "component admin" role in the reduced-scope model. edit/
    // delete are scoped to (projectKey, componentId) together, not just
    // componentId, the same IDOR-safe pattern used by worklogs/comments/
    // attachments: a component id belonging to a different project is
    // treated as not found, never silently acted on.
    std::vector<Domain::ProjectComponent> listComponents(const std::string& projectKey,
                                                          const std::optional<Domain::Principal>& actor);
    Domain::ProjectComponent createComponent(Domain::CreateComponentRequest request, const Domain::Principal& actor);
    std::optional<Domain::ProjectComponent> editComponent(const std::string& projectKey,
                                                           const std::string& componentId,
                                                           Domain::EditComponentRequest request,
                                                           const Domain::Principal& actor);
    bool deleteComponent(const std::string& projectKey, const std::string& componentId, const Domain::Principal& actor);

    // --- Project lifecycle (Phase 2, D3/D87/D88/D89) ---
    // Creation requires global administrator: there is no project to hold a
    // project-admin membership row over until it exists, matching the
    // admin-only account creation model of V1 (D2).
    Domain::Project createProject(Domain::CreateProjectRequest request, const Domain::Principal& actor);
    // Archiving and moving to the recycle bin are project-management actions:
    // project admin (or global admin) suffices.
    bool setProjectArchived(const std::string& projectKey, bool archived, const Domain::Principal& actor);
    // Changing an active project's key (D91): project-Admin-or-above, the
    // same level as archiving/deleting a project. Returns nullopt if
    // projectKey does not resolve to a live project; throws
    // std::invalid_argument for an invalid newKey, a no-op rename (new key
    // identical to the current one), or a newKey already in use.
    std::optional<Domain::Project> changeProjectKey(const std::string& projectKey, const std::string& newKey,
                                                     const Domain::Principal& actor);
    bool deleteProject(const std::string& projectKey, const Domain::Principal& actor);
    // Restoring, listing, and permanently deleting from the recycle bin are
    // global-administrator-only, per D88 ("admin restore or permanent delete").
    bool restoreProject(const std::string& projectKey, const Domain::Principal& actor);
    std::vector<Domain::Project> listDeletedProjects(const Domain::Principal& actor);
    bool permanentlyDeleteProject(const std::string& projectKey, const Domain::Principal& actor);

    // --- User directory (Phase 4, D80) ---
    // Just enough to support @mention autocomplete and other user pickers --
    // requires an authenticated session (not anonymous, even when the
    // installation-wide anonymous-read toggle is on), since the user
    // directory is more sensitive than ticket data.
    std::vector<Domain::User> listUsers(const Domain::Principal& actor);

    // --- Timezone/clock-format preferences (D45) ---
    // Self-service, own-account-only; no admin management of another user's
    // preference. `updatePreferences` returns the updated Principal (same
    // shape `/api/v1/auth/me` returns) so the caller can refresh its cached
    // copy without a second round trip.
    Domain::Principal updatePreferences(const Domain::UpdatePreferencesRequest& request, const Domain::Principal& actor);

    // --- Fixed in-app notifications (Phase 4, D14) ---
    // Exactly three types, created as a side effect of assigning a ticket,
    // being @mentioned in a new comment, or a new comment landing on an
    // ticket the recipient watches -- see addComment/createTicket/editTicket.
    // Always scoped to the caller's own notifications; there is no
    // cross-user notification management.
    std::vector<Domain::Notification> listNotifications(const Domain::Principal& actor, bool unreadOnly);
    int countUnreadNotifications(const Domain::Principal& actor);
    bool markNotificationRead(const std::string& notificationId, const Domain::Principal& actor);
    bool markAllNotificationsRead(const Domain::Principal& actor);

    // --- Simple append-only admin/security audit log (Phase 4, D23) ---
    // Global-administrator-only, like the recycle bins and user directory.
    // No categories/export/configurable retention -- just a capped,
    // newest-first read of everything recorded so far.
    std::vector<Domain::AuditEvent> listAuditEvents(const Domain::Principal& actor, int limit = 200);

private:
    std::shared_ptr<Infrastructure::Database::IDatabase> database_;
    Infrastructure::Storage::LocalAttachmentStorage attachmentStorage_;

    void requireProjectRole(const Domain::Principal& actor, const std::string& projectKey, int minimumRank) const;
    void requireGlobalAdmin(const Domain::Principal& actor) const;
    void requireReadAccess(const std::optional<Domain::Principal>& actor);
    void requireValidHierarchy(Domain::CreateTicketRequest& request);
    // Shared by requireValidHierarchy (create) and editTicket (re-typing/
    // re-parenting, D5/D29/D64-D66): validates and normalizes a type/parent
    // combination against the fixed hierarchy rules for a project.
    // `excludeSelfKey` is the ticket's own key during an edit (rejects
    // self-parenting) and nullopt during create (the ticket doesn't exist
    // yet, so it cannot already be a candidate parent).
    void validateHierarchyShape(const std::string& ticketTypeKey,
                                std::optional<std::string>& parentTicketKey,
                                const std::string& projectKey,
                                const std::optional<std::string>& excludeSelfKey);

    // Notifies a newly-set assignee (D14 "assigned to me"), skipping a
    // self-assignment and a no-op re-save with the same assignee.
    void dispatchAssignmentNotification(const Domain::Ticket& ticketAfter,
                                        const std::optional<Domain::UserSummary>& assigneeBefore,
                                        const Domain::Principal& actor);
    // Notifies @handle mentions found in a just-created comment body (D80)
    // and every watcher of the ticket except the comment's own author (D14
    // "comment on a watched ticket"). A user who is both mentioned and a
    // watcher gets only the "mentioned" notification, not both -- one
    // notification per comment per recipient, the more specific reason
    // wins. Only called from addComment -- editing a comment does not
    // re-scan for new mentions, to avoid re-notifying on every save of an
    // already-mentioning comment.
    void dispatchCommentNotifications(const Domain::Ticket& ticket, const Domain::Comment& comment, const Domain::Principal& actor);
};

} // namespace TicketHub::Application
