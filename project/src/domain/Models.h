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

struct Project {
    std::string id;
    std::string key;
    std::string name;
    std::string description;
    std::optional<UserSummary> lead;
    std::int64_t issueCount{};
    std::int64_t openIssueCount{};
};

struct IssueType {
    std::string key;
    std::string name;
    std::string icon;
    std::string color;
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
