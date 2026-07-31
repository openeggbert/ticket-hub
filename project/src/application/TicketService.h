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
