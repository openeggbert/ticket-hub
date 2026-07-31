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

    // Every read use case takes the caller's Principal explicitly, as
    // std::nullopt for an anonymous (unauthenticated) caller. Project
    // visibility does not otherwise depend on membership -- any authenticated
    // user sees all projects (D58); an anonymous caller is let through only if
    // the installation's anonymous read-access toggle is on (D59, off by
    // default), otherwise Domain::AuthenticationRequired is thrown.
    std::vector<Domain::Project> listProjects(const std::optional<Domain::Principal>& actor);
    std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter, const std::optional<Domain::Principal>& actor);
    std::optional<Domain::Issue> findIssue(const std::string& issueKey, const std::optional<Domain::Principal>& actor);
    std::vector<Domain::Comment> listComments(const std::string& issueKey, const std::optional<Domain::Principal>& actor);
    Domain::DashboardStats dashboard(const std::optional<Domain::Principal>& actor);

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

private:
    std::shared_ptr<Infrastructure::Database::IDatabase> database_;

    void requireProjectRole(const Domain::Principal& actor, const std::string& projectKey, int minimumRank) const;
    void requireGlobalAdmin(const Domain::Principal& actor) const;
    void requireReadAccess(const std::optional<Domain::Principal>& actor);
    void requireValidHierarchy(Domain::CreateIssueRequest& request);
};

} // namespace TicketHub::Application
