#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace TicketHub::Domain {

struct UserSummary {
    std::string id;
    std::string displayName;
    std::string email;
};

// Full identity record for a local account. There is no `handle` field yet --
// it is introduced in a later phase together with @mentions, which is the
// first feature that actually needs one.
struct User {
    std::string id;
    std::string email;
    std::string displayName;
    std::string timeZone{"UTC"};
    std::string clockFormat{"24h"};
    bool active{true};
    bool isAdmin{false};
    std::string createdAt;
};

// The actor context threaded through every application write use case,
// replacing the prototype's hardcoded demo-user assumption. Built from a
// validated session (web) or, in a later phase, a validated PAT (API).
struct Principal {
    std::string userId;
    std::string email;
    std::string displayName;
    bool isAdmin{false};
};

// Administrator-only account creation. There is no public registration and
// no invitation flow in V1: the admin sets the password directly, and there
// is no forced-change-on-first-login flag -- the user may change it later
// through the ordinary "change my password" action if one exists.
struct CreateUserRequest {
    std::string email;
    std::string displayName;
    std::string password;
    bool isAdmin{false};
};

struct LoginRequest {
    std::string email;
    std::string password;
};

struct Session {
    std::string id;
    std::string userId;
    std::string createdAt;
    std::string expiresAt;
};

struct AuthenticatedSession {
    Session session;
    std::string sessionToken; // returned to the caller exactly once, at creation
};

// Fixed project roles (Phase 2, D3). Replaces the original plan's
// configurable permission schemes -- there is no admin UI to define new
// roles or grant targets in V1. `ProjectRoleRank` returns -1 for an unknown
// role string, so callers can treat "no membership row" and "unrecognized
// role" the same way (no access).
constexpr const char* ProjectRoleViewer = "viewer";
constexpr const char* ProjectRoleMember = "member";
constexpr const char* ProjectRoleAdmin = "admin";

inline int projectRoleRank(const std::string& role) {
    if (role == ProjectRoleViewer) {
        return 0;
    }
    if (role == ProjectRoleMember) {
        return 1;
    }
    if (role == ProjectRoleAdmin) {
        return 2;
    }
    return -1;
}

struct Project {
    std::string id;
    std::string key;
    std::string name;
    std::string description;
    std::optional<UserSummary> lead;
    std::int64_t issueCount{};
    std::int64_t openIssueCount{};
    bool archived{false};
};

struct CreateProjectRequest {
    std::string key;
    std::string name;
    std::string description;
};

struct IssueType {
    std::string key;
    std::string name;
    std::string icon;
    std::string color;
};

// Fixed issue types and the fixed hierarchy they imply (D5, D29, D64-D66):
// Epic -> Story/Task/Bug -> Sub-task, with nothing above Epic and nothing
// below Sub-task. There are no custom types in V1, so this is a hardcoded
// table rather than driven by the `issue_types.hierarchy_level` column.
constexpr const char* IssueTypeEpic = "epic";
constexpr const char* IssueTypeStory = "story";
constexpr const char* IssueTypeTask = "task";
constexpr const char* IssueTypeBug = "bug";
constexpr const char* IssueTypeSubTask = "sub-task";

// 1 = Epic, 0 = Story/Task/Bug (or any unrecognized type, which the database
// FK lookup will reject anyway), -1 = Sub-task.
inline int issueTypeHierarchyLevel(const std::string& issueTypeKey) {
    if (issueTypeKey == IssueTypeEpic) {
        return 1;
    }
    if (issueTypeKey == IssueTypeSubTask) {
        return -1;
    }
    return 0;
}

// Fixed resolutions (D27/D28), matching the `issues.resolution` CHECK
// constraint in migrations/*/001_initial.sql. Required on the transition to
// a Done-category status and cleared automatically on reopen (D68-D70) --
// there is no other way to set or clear it.
constexpr const char* ResolutionFixed = "fixed";
constexpr const char* ResolutionDone = "done";
constexpr const char* ResolutionWontFix = "wont-fix";
constexpr const char* ResolutionDuplicate = "duplicate";
constexpr const char* ResolutionCannotReproduce = "cannot-reproduce";

inline bool isValidResolution(const std::string& resolutionKey) {
    return resolutionKey == ResolutionFixed || resolutionKey == ResolutionDone
        || resolutionKey == ResolutionWontFix || resolutionKey == ResolutionDuplicate
        || resolutionKey == ResolutionCannotReproduce;
}

// Fixed issue-link catalog (D17): a larger built-in set instead of
// admin-configurable link types. `clones`/`is cloned by` is also the link
// TicketService::cloneIssue creates automatically (D60).
constexpr const char* LinkTypeBlocks = "blocks";
constexpr const char* LinkTypeRelatesTo = "relates_to";
constexpr const char* LinkTypeDuplicates = "duplicates";
constexpr const char* LinkTypeClones = "clones";

inline bool isValidLinkType(const std::string& linkType) {
    return linkType == LinkTypeBlocks || linkType == LinkTypeRelatesTo
        || linkType == LinkTypeDuplicates || linkType == LinkTypeClones;
}

// A link is stored as one directed row (source "blocks" target), but is
// meaningful read from either end -- `outward` says which label applies to
// the issue this label pair is being shown on. `relates_to` is symmetric by
// convention (same label either way), matching Jira's own behavior.
struct LinkTypeLabels {
    std::string outward;
    std::string inward;
};

inline LinkTypeLabels linkTypeLabels(const std::string& linkType) {
    if (linkType == LinkTypeBlocks) {
        return {"blocks", "is blocked by"};
    }
    if (linkType == LinkTypeDuplicates) {
        return {"duplicates", "is duplicated by"};
    }
    if (linkType == LinkTypeClones) {
        return {"clones", "is cloned by"};
    }
    return {"relates to", "relates to"};
}

// One link as seen from a specific issue (the one `listIssueLinks` was
// called with) -- `outward` is true when that issue is the link's source.
struct IssueLink {
    std::string id;
    std::string linkType;
    bool outward{true};
    std::string otherIssueKey;
    std::string otherIssueSummary;
};

// A link with both ends resolved to their project, for authorization checks
// that must confirm the actor has access to both sides before creating or
// deleting a link (a link write is not scoped to a single project).
struct IssueLinkDetail {
    std::string id;
    std::string linkType;
    std::string sourceIssueKey;
    std::string sourceProjectKey;
    std::string targetIssueKey;
    std::string targetProjectKey;
};

struct Status {
    std::string key;
    std::string name;
    std::string category;
    int sortOrder{};
};

struct Priority {
    std::string key;
    std::string name;
    int rank{};
    std::string color;
};

struct Comment {
    std::string id;
    std::string issueId;
    UserSummary author;
    std::string body;
    std::string createdAt;
    std::string updatedAt;
};

struct Issue {
    std::string id;
    std::string key;
    std::int64_t number{};
    std::string projectKey;
    std::string projectName;
    std::string summary;
    std::string description;
    IssueType type;
    Status status;
    Priority priority;
    UserSummary reporter;
    std::optional<UserSummary> assignee;
    std::optional<std::string> parentIssueKey;
    std::optional<double> storyPoints;
    std::optional<std::string> dueDate;
    std::optional<std::string> resolution;
    std::vector<std::string> labels;
    std::string createdAt;
    std::string updatedAt;
    std::int64_t version{1};
};

struct IssueFilter {
    std::optional<std::string> projectKey;
    std::optional<std::string> statusKey;
    std::optional<std::string> search;
};

struct CreateIssueRequest {
    std::string projectKey;
    std::string summary;
    std::string description;
    std::string issueTypeKey{"task"};
    std::string priorityKey{"medium"};
    std::optional<std::string> assigneeEmail;
    // Epic link (for Story/Task/Bug) or required parent (for Sub-task) --
    // see Domain::issueTypeHierarchyLevel and TicketService::createIssue.
    std::optional<std::string> parentIssueKey;
    std::vector<std::string> labels;
    std::optional<double> storyPoints;
    std::optional<std::string> dueDate;
};

// Full-replacement edit of an issue's standard fields, applied with the same
// optimistic-locking contract as changeIssueStatus (D129). This is a PUT-style
// request, not a JSON-merge-patch: every field here is always the caller's
// intended final value (an absent optional field means "no value", not
// "leave whatever is there alone") -- the caller is expected to pre-populate
// an edit form/request from the current issue. `issueTypeKey` and
// `parentIssueKey` are intentionally not editable yet; re-typing or
// re-parenting an issue after creation is not yet implemented (see NEXT.md).
struct EditIssueRequest {
    std::string summary;
    std::string description;
    std::string priorityKey;
    std::optional<std::string> assigneeEmail;
    std::vector<std::string> labels;
    std::optional<double> storyPoints;
    std::optional<std::string> dueDate;
};

struct AddCommentRequest {
    std::string issueKey;
    std::string body;
};

struct DashboardStats {
    std::int64_t totalIssues{};
    std::int64_t todoIssues{};
    std::int64_t inProgressIssues{};
    std::int64_t doneIssues{};
    std::vector<Issue> recentIssues;
};

} // namespace TicketHub::Domain
