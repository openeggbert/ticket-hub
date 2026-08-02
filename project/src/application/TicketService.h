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
    std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter, const std::optional<Domain::Principal>& actor);
    // Numbered/offset pagination (D126), used by GET /api/v1/issues. The
    // unpaginated overload above remains for internal/CSV-export use where
    // "everything matching the filter" is the intended semantics.
    Domain::Page<Domain::Issue> listIssuesPaged(const Domain::IssueFilter& filter, int page, int pageSize,
                                                const std::optional<Domain::Principal>& actor);
    std::optional<Domain::Issue> findIssue(const std::string& issueKey, const std::optional<Domain::Principal>& actor);
    std::vector<Domain::Comment> listComments(const std::string& issueKey, const std::optional<Domain::Principal>& actor);
    Domain::DashboardStats dashboard(const std::optional<Domain::Principal>& actor);

    // Kanban board WIP limits (D32/D33): a single flat, installation-wide
    // list (same read-access rule as projects/issues). Setting a limit is
    // global-administrator-only, like the anonymous-read toggle -- there is
    // no per-project board admin concept in the reduced-scope model.
    std::vector<Domain::BoardColumn> listBoardColumns(const std::optional<Domain::Principal>& actor);
    void setBoardColumnWipLimit(const std::string& statusKey, std::optional<int> wipLimit,
                                const Domain::Principal& actor);

    bool isAnonymousReadEnabled();
    // Global-administrator-only: it is an installation-wide toggle, not a
    // per-project setting.
    void setAnonymousReadEnabled(bool enabled, const Domain::Principal& actor);

    // Every write use case takes the caller's Principal explicitly (never
    // optional) -- there is no anonymous write path and no fixed demo-user
    // fallback. Callers (the web layer, once wired to Crow) are responsible
    // for resolving a Principal from an authenticated session or PAT before
    // calling these.
    // Validates the fixed Epic/Sub-task hierarchy rules (D64-D66) against
    // `request.parentIssueKey` before creating the issue, throwing
    // std::invalid_argument on a violation (same as any other input
    // validation failure -- these are structural rules on the request, not a
    // Domain::WorkflowViolation, which is reserved for rules that depend on
    // an issue's current, mutable state).
    Domain::Issue createIssue(Domain::CreateIssueRequest request, const Domain::Principal& actor);
    // `resolution` is required exactly when `statusKey` names a Done-category
    // status, ignored otherwise, and forced to null when leaving a
    // Done-category status (D68-D70); see IDatabase::changeIssueStatus.
    bool changeStatus(const std::string& issueKey,
                      const std::string& statusKey,
                      const Domain::Principal& actor,
                      std::optional<std::string> resolution = std::nullopt,
                      std::optional<std::int64_t> expectedVersion = std::nullopt);
    // Full-replacement edit of an issue's standard fields (D129); see
    // Domain::EditIssueRequest for the PUT-style contract and
    // IDatabase::editIssue for the optimistic-locking/history behavior.
    // Returns nullopt if the issue does not exist.
    std::optional<Domain::Issue> editIssue(const std::string& issueKey,
                                           Domain::EditIssueRequest request,
                                           const Domain::Principal& actor,
                                           std::optional<std::int64_t> expectedVersion = std::nullopt);
    Domain::Comment addComment(const std::string& issueKey, const std::string& body, const Domain::Principal& actor);

    // --- Comment editing and tombstone delete (Phase 4, D81/D82/D83) ---
    // Simplified permissions (D83): the comment's own author may always edit
    // or delete it; otherwise the actor needs project-Admin-or-above on the
    // comment's issue's project (or global admin) -- there is no separate
    // edit-own/edit-all/delete-own/delete-all permission matrix. Returns
    // nullopt/false if the comment (or its issue) does not exist.
    std::optional<Domain::Comment> editComment(const std::string& issueKey,
                                               const std::string& commentId,
                                               const std::string& body,
                                               const Domain::Principal& actor,
                                               std::optional<std::int64_t> expectedVersion = std::nullopt);
    bool deleteComment(const std::string& issueKey, const std::string& commentId, const Domain::Principal& actor);

    // --- Fixed emoji reactions on comments (Phase 4, D84) ---
    // Self-service only, same reasoning as watch/vote (D20/D79): no
    // project-role check, just an authenticated actor and an existing
    // comment. `reactionKey` must be one of Domain::isValidCommentReactionKey,
    // and both the issue and the comment must exist, or this throws
    // std::invalid_argument (matching watchIssue's own unknown-issue
    // behavior, rather than editComment/deleteComment's nullopt/false
    // not-found convention). add/remove return true only when a row was
    // actually inserted/removed -- reacting (or un-reacting) twice with the
    // same key is a no-op, matching watch/vote.
    bool addCommentReaction(const std::string& issueKey, const std::string& commentId,
                            const std::string& reactionKey, const Domain::Principal& actor);
    bool removeCommentReaction(const std::string& issueKey, const std::string& commentId,
                               const std::string& reactionKey, const Domain::Principal& actor);
    std::vector<Domain::CommentReaction> listCommentReactions(const std::string& issueKey,
                                                               const std::string& commentId,
                                                               const std::optional<Domain::Principal>& actor);

    // --- Simplified worklogs (Phase 4, D12/D13) ---
    // No own-vs-others permission split (D13): add/edit/delete all require
    // the same project-Member-or-above level as any other issue write --
    // any project member may edit or delete any worklog on an issue they
    // can access, not just the one they logged themselves. edit/delete
    // return nullopt/false if the worklog (or its issue) does not exist.
    std::vector<Domain::Worklog> listWorklogs(const std::string& issueKey, const std::optional<Domain::Principal>& actor);
    Domain::Worklog addWorklog(const std::string& issueKey,
                               const std::string& workDate,
                               std::int64_t timeSpentSeconds,
                               std::optional<std::string> comment,
                               const Domain::Principal& actor);
    std::optional<Domain::Worklog> editWorklog(const std::string& issueKey,
                                               const std::string& worklogId,
                                               const std::string& workDate,
                                               std::int64_t timeSpentSeconds,
                                               std::optional<std::string> comment,
                                               const Domain::Principal& actor,
                                               std::optional<std::int64_t> expectedVersion = std::nullopt);
    bool deleteWorklog(const std::string& issueKey, const std::string& worklogId, const Domain::Principal& actor);

    // Attachments (Phase 5, D15/D98-D105). Read access mirrors comments/
    // issues (any authenticated user, or anonymous if the installation
    // toggle is on). Uploading requires project-Member-or-above on the
    // issue's project, the same level as every other issue write. Deleting
    // is uploader-or-project-Admin-or-above -- no decision text specifies
    // this, so it mirrors D83's comment edit/delete rule as the closest
    // precedent (both are "content a specific user added to an issue").
    // The recycle bin (list deleted/restore/permanent-delete) is
    // global-administrator-only, the same split as the issue and project
    // recycle bins.
    std::vector<Domain::Attachment> listAttachments(const std::string& issueKey,
                                                     const std::optional<Domain::Principal>& actor);
    Domain::Attachment uploadAttachment(const std::string& issueKey,
                                        const std::string& fileName,
                                        const std::string& contentType,
                                        const std::string& bytes,
                                        const Domain::Principal& actor);
    // Returns the attachment's metadata alongside its raw bytes -- the
    // download route needs both (bytes for the body, metadata for the
    // filename/content-type response headers).
    std::pair<Domain::Attachment, std::string> downloadAttachment(const std::string& attachmentId,
                                                                   const std::optional<Domain::Principal>& actor);
    bool deleteAttachment(const std::string& issueKey, const std::string& attachmentId, const Domain::Principal& actor);
    std::vector<Domain::Attachment> listDeletedAttachments(const Domain::Principal& actor);
    bool restoreAttachment(const std::string& attachmentId, const Domain::Principal& actor);
    bool permanentlyDeleteAttachment(const std::string& attachmentId, const Domain::Principal& actor);

    // Simple field-copy clone (D60): summary/description/type/priority/labels
    // into a new issue in the same project, plus a `clones`/`is cloned by`
    // link back to the original. Assignee, story points, due date, and
    // (except the one structurally-required case below) the parent/Epic link
    // are not copied. Requires project-Member-or-above, same as createIssue.
    // Special case: a Sub-task cannot exist without a parent (D64), so
    // cloning a Sub-task keeps its original parent -- this is a structural
    // requirement for the clone to be valid at all, not "copying the
    // hierarchy" in the sense the decision excludes.
    Domain::Issue cloneIssue(const std::string& issueKey, const Domain::Principal& actor);

    // --- Manual ordering (Phase 3, D31) ---
    // Requires project-Member-or-above on the issue's own project. A
    // `beforeIssueKey` in a different project is rejected by the database
    // layer with std::invalid_argument before any role check on it would be
    // meaningful (reordering is always a single-project operation).
    Domain::Issue reorderIssue(const std::string& issueKey,
                               std::optional<std::string> beforeIssueKey,
                               const Domain::Principal& actor);

    // --- Move between projects (Phase 3, D37) ---
    // Requires project-Member-or-above on both the source and target
    // projects, mirroring createIssueLink's two-project-role-check pattern.
    Domain::Issue moveIssue(const std::string& issueKey,
                            const std::string& targetProjectKey,
                            const Domain::Principal& actor);

    // --- Issue links (Phase 3, D17) ---
    // Both ends of a link must be project-Member-or-above for the actor,
    // since a link write touches two issues that may be in different
    // projects (unlike other issue writes, which touch exactly one).
    Domain::IssueLink createIssueLink(const std::string& sourceIssueKey,
                                      const std::string& targetIssueKey,
                                      const std::string& linkType,
                                      const Domain::Principal& actor);
    std::vector<Domain::IssueLink> listIssueLinks(const std::string& issueKey,
                                                   const std::optional<Domain::Principal>& actor);
    // Returns false if the link does not exist.
    bool deleteIssueLink(const std::string& linkId, const Domain::Principal& actor);

    // --- Watchers and voting (Phase 3, D20/D79) ---
    // Self-service only -- no managing other users' watch/vote state, and no
    // project-role check: only the issue itself needs to exist. Any
    // authenticated user may watch/vote on any issue (D58: any authenticated
    // user sees all projects; roles gate writes only, and watch/vote are the
    // one exception even to that, since Jira gates them by "browse" access
    // rather than a write-capable role). watch/vote return true only when the
    // row was newly added; unwatch/unvote return true only when a row was
    // actually removed. All throw std::invalid_argument for an unknown issue.
    bool watchIssue(const std::string& issueKey, const Domain::Principal& actor);
    bool unwatchIssue(const std::string& issueKey, const Domain::Principal& actor);
    std::vector<Domain::UserSummary> listWatchers(const std::string& issueKey,
                                                   const std::optional<Domain::Principal>& actor);
    bool voteIssue(const std::string& issueKey, const Domain::Principal& actor);
    bool unvoteIssue(const std::string& issueKey, const Domain::Principal& actor);
    std::vector<Domain::UserSummary> listVoters(const std::string& issueKey,
                                                 const std::optional<Domain::Principal>& actor);

    // --- Issue recycle bin (Phase 3, D22) ---
    // Mirrors the project recycle bin (Phase 2): soft-delete requires
    // project-Admin-or-above (like deleteProject); restoring, listing, and
    // permanently deleting are global-administrator-only, the same
    // "admin restore or permanent delete" split used for projects (D88).
    bool deleteIssue(const std::string& issueKey, const Domain::Principal& actor);
    bool restoreIssue(const std::string& issueKey, const Domain::Principal& actor);
    std::vector<Domain::Issue> listDeletedIssues(const Domain::Principal& actor);
    bool permanentlyDeleteIssue(const std::string& issueKey, const Domain::Principal& actor);

    // --- Simple bulk actions (Phase 3, D36) ---
    // Each issue key is processed independently through the corresponding
    // single-issue operation above -- same authorization, same validation,
    // same workflow rules -- so a bulk call is exactly as safe as doing each
    // action one at a time. No cross-project move and no type change in
    // bulk (D36 explicitly excludes both).
    Domain::BulkActionResult bulkChangeStatus(const std::vector<std::string>& issueKeys,
                                              const std::string& statusKey,
                                              std::optional<std::string> resolution,
                                              const Domain::Principal& actor);
    Domain::BulkActionResult bulkAssign(const std::vector<std::string>& issueKeys,
                                        std::optional<std::string> assigneeEmail,
                                        const Domain::Principal& actor);
    Domain::BulkActionResult bulkAddLabel(const std::vector<std::string>& issueKeys,
                                          const std::string& label,
                                          const Domain::Principal& actor);
    Domain::BulkActionResult bulkDelete(const std::vector<std::string>& issueKeys, const Domain::Principal& actor);

    // --- Project lifecycle (Phase 2, D3/D87/D88/D89) ---
    // Creation requires global administrator: there is no project to hold a
    // project-admin membership row over until it exists, matching the
    // admin-only account creation model of V1 (D2).
    Domain::Project createProject(Domain::CreateProjectRequest request, const Domain::Principal& actor);
    // Archiving and moving to the recycle bin are project-management actions:
    // project admin (or global admin) suffices.
    bool setProjectArchived(const std::string& projectKey, bool archived, const Domain::Principal& actor);
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
    // directory is more sensitive than issue data.
    std::vector<Domain::User> listUsers(const Domain::Principal& actor);

    // --- Fixed in-app notifications (Phase 4, D14) ---
    // Exactly three types, created as a side effect of assigning an issue,
    // being @mentioned in a new comment, or a new comment landing on an
    // issue the recipient watches -- see addComment/createIssue/editIssue.
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
    void requireValidHierarchy(Domain::CreateIssueRequest& request);

    // Notifies a newly-set assignee (D14 "assigned to me"), skipping a
    // self-assignment and a no-op re-save with the same assignee.
    void dispatchAssignmentNotification(const Domain::Issue& issueAfter,
                                        const std::optional<Domain::UserSummary>& assigneeBefore,
                                        const Domain::Principal& actor);
    // Notifies @handle mentions found in a just-created comment body (D80)
    // and every watcher of the issue except the comment's own author (D14
    // "comment on a watched issue"). A user who is both mentioned and a
    // watcher gets only the "mentioned" notification, not both -- one
    // notification per comment per recipient, the more specific reason
    // wins. Only called from addComment -- editing a comment does not
    // re-scan for new mentions, to avoid re-notifying on every save of an
    // already-mentioning comment.
    void dispatchCommentNotifications(const Domain::Issue& issue, const Domain::Comment& comment, const Domain::Principal& actor);
};

} // namespace TicketHub::Application
