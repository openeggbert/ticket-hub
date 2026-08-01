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

// Full identity record for a local account.
struct User {
    std::string id;
    std::string email;
    std::string displayName;
    // Optional, unique, lowercase (D56); the @mention target (D80). No
    // self-service profile editing exists yet -- an admin sets it via
    // `ticket-hub-cli create-user ... --handle=<handle>` at creation time.
    std::optional<std::string> handle;
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
    std::optional<std::string> handle;
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

// Result of a simple bulk action (D36): each issue key is processed
// independently (no cross-issue transaction), so a partial failure -- an
// unknown key, insufficient project role, a workflow rule violation -- does
// not roll back the keys that already succeeded. No per-item error detail;
// "simple bulk actions" does not call for it.
struct BulkActionResult {
    std::vector<std::string> succeeded;
    std::vector<std::string> failed;
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
    std::int64_t version{1};
    // Set once a comment is edited (D81); replaces a full version-history
    // table -- only "this was edited at X" is kept, not the prior text.
    std::optional<std::string> editedAt;
};

// Attachments (D15/D98-D105): local filesystem storage only, hardwired --
// `storageKey` is an opaque handle into that local store, not a
// discriminated union over multiple backends, since no other backend
// exists or is planned for V1. `sha256` is computed once, at upload
// (D105); there is no periodic re-verification. Attached to an issue
// directly (not to an individual comment) so it can be referenced via
// `attachment://<id>` from the issue description or from any comment on
// that issue (D100).
struct Attachment {
    std::string id;
    std::string issueId;
    // Resolved via a join purely for display convenience (e.g. the
    // attachment recycle bin, which spans every issue and would otherwise
    // have nothing human-readable to show); routes are always nested under
    // `/api/issues/{key}/attachments`, so ordinary reads never need this.
    std::string issueKey;
    UserSummary uploader;
    std::string fileName;
    std::string contentType;
    std::int64_t byteSize{};
    std::string sha256;
    std::string createdAt;
    // Set only when returned from the recycle bin (`listDeletedAttachments`);
    // nullopt for an active attachment. Unlike issues/projects, whose fixed
    // 90-day on-demand purge (D102) is a single DELETE entirely inside the
    // database adapter, an attachment's purge must also delete its file on
    // disk -- something only the application layer (TicketService) can do
    // -- so it needs this timestamp to decide what has aged out.
    std::optional<std::string> deletedAt;
};

// Fixed emoji reaction catalog (D84): the decision register calls for "a
// fixed reaction set" on comments without enumerating one, so this uses
// GitHub's well-known eight-reaction set as a conservative, familiar
// default. Each user may add each reaction at most once per comment
// (enforced by the `comment_reactions` composite primary key), matching
// D84's own wording; issues keep the separate, unrelated voting feature
// (D79).
constexpr const char* CommentReactionThumbsUp = "thumbs_up";
constexpr const char* CommentReactionThumbsDown = "thumbs_down";
constexpr const char* CommentReactionLaugh = "laugh";
constexpr const char* CommentReactionHooray = "hooray";
constexpr const char* CommentReactionConfused = "confused";
constexpr const char* CommentReactionHeart = "heart";
constexpr const char* CommentReactionRocket = "rocket";
constexpr const char* CommentReactionEyes = "eyes";

inline bool isValidCommentReactionKey(const std::string& reactionKey) {
    return reactionKey == CommentReactionThumbsUp || reactionKey == CommentReactionThumbsDown
        || reactionKey == CommentReactionLaugh || reactionKey == CommentReactionHooray
        || reactionKey == CommentReactionConfused || reactionKey == CommentReactionHeart
        || reactionKey == CommentReactionRocket || reactionKey == CommentReactionEyes;
}

// One (comment, user, reaction) row, as returned by `listCommentReactions` --
// the API layer groups these by `reactionKey` into per-reaction counts and
// user lists.
struct CommentReaction {
    std::string reactionKey;
    UserSummary user;
};

// Fixed in-app notification set (D14): exactly these three types, no
// admin-configurable schemes, no email, no per-user preferences/digests.
// "mentioned" comes from @handle tokens parsed out of a comment body at
// creation time (D80); description/other Markdown fields are not scanned
// for mentions in V1, since none of them have the autocomplete affordance
// that makes a mention discoverable while typing.
constexpr const char* NotificationTypeAssigned = "assigned";
constexpr const char* NotificationTypeMentioned = "mentioned";
constexpr const char* NotificationTypeWatchedComment = "watched_comment";

// `issueKey`/`issueSummary` are resolved at read time from the stored
// `issue_id` (nullable in principle, but every current notification type
// always has one) -- there is no stored message string, matching the
// minimal `user_id, type, issue_id, read_at` shape in
// docs/REDUCED_SCOPE_DATA_MODEL.md; the UI builds display text from
// `type` + the resolved issue.
struct Notification {
    std::string id;
    std::string type;
    std::optional<std::string> issueKey;
    std::optional<std::string> issueSummary;
    std::optional<std::string> readAt;
    std::string createdAt;
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
    // Simple integer manual order within its project (D31), replacing the
    // never-used LexoRank-style string rank. New issues are appended
    // (highest existing rankOrder + 1); TicketService::reorderIssue
    // renumbers the issues between the old and new position by 1 each,
    // rather than using fractional/string ranks.
    std::int64_t rankOrder{0};
};

// Ad-hoc in-UI filters only (D10): no saved/shared filters, no JQL, not
// usable as a webhook/board source. `search` is a plain case-insensitive
// substring match (LIKE/ILIKE) against summary/description/issue key, no
// full-text index (D43). `dueBefore` is inclusive ("due on or before this
// date").
struct IssueFilter {
    std::optional<std::string> projectKey;
    std::optional<std::string> statusKey;
    std::optional<std::string> issueTypeKey;
    std::optional<std::string> priorityKey;
    std::optional<std::string> assigneeEmail;
    std::optional<std::string> label;
    std::optional<std::string> dueBefore;
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

// Simplified worklogs (Phase 4, D12/D13): time spent + an optional comment
// only -- no remaining-estimate linkage (D12 dropped time estimates from
// V1 entirely, so there is nothing for a worklog to adjust) and no
// separate own-vs-others edit/delete permission split (D13: any project
// member with issue access may edit or delete any worklog on that issue,
// not just the one they logged -- see TicketService::editWorklog/
// deleteWorklog).
struct Worklog {
    std::string id;
    std::string issueId;
    UserSummary author;
    std::string workDate; // ISO date, "YYYY-MM-DD"
    std::int64_t timeSpentSeconds{};
    std::optional<std::string> comment;
    std::string createdAt;
    std::string updatedAt;
    std::int64_t version{1};
};

struct AddWorklogRequest {
    std::string issueKey;
    std::string workDate;
    std::int64_t timeSpentSeconds{};
    std::optional<std::string> comment;
};

struct EditWorklogRequest {
    std::string workDate;
    std::int64_t timeSpentSeconds{};
    std::optional<std::string> comment;
};

// Simple append-only admin/security audit log (Phase 4, D23): no
// categories/export/configurable retention beyond what's here -- rows are
// never auto-purged. `actor` is nullopt for an event with no authenticated
// actor (a failed/blocked login attempt, or CLI-driven account creation,
// which runs outside any web session).
struct AuditEvent {
    std::string id;
    std::string category;
    std::string action;
    std::optional<UserSummary> actor;
    std::optional<std::string> targetType;
    std::optional<std::string> targetId;
    std::optional<std::string> details;
    std::string createdAt;
};

// Fixed personal dashboard (D24): assigned issues, watched issues, recent
// activity, deadlines, simple stats -- no active-sprint widget (Scrum was
// removed for V1). `recentIssues`/the four counts stay installation-wide;
// `assignedToMe`/`watchedIssues`/`upcomingDeadlines` are empty for an
// anonymous viewer (no personal identity to personalize for) and otherwise
// scoped to the requesting actor. `assignedToMe` and `upcomingDeadlines`
// both exclude Done-category issues (an already-finished issue isn't
// something to act on); `upcomingDeadlines` is further limited to issues
// that have a due date, soonest first.
struct DashboardStats {
    std::int64_t totalIssues{};
    std::int64_t todoIssues{};
    std::int64_t inProgressIssues{};
    std::int64_t doneIssues{};
    std::vector<Issue> recentIssues;
    std::vector<Issue> assignedToMe;
    std::vector<Issue> watchedIssues;
    std::vector<Issue> upcomingDeadlines;
};

// Kanban board WIP limits (D32/D33): D32 keeps "one board column equals
// one workflow status", so this is a single flat, installation-wide list
// -- one row per fixed workflow status, not one per project per status.
// `wipLimit` is nullopt for "no limit" and is always soft: a
// display-time-only comparison against a column's live issue count, never
// enforced server-side (an over-limit column is highlighted, not
// blocked).
struct BoardColumn {
    std::string id;
    std::string statusKey;
    std::string statusName;
    int sortOrder{};
    std::optional<int> wipLimit;
};

} // namespace TicketHub::Domain
