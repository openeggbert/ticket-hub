#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace TicketHub::Domain {

struct UserSummary {
    std::string id;
    std::string username;
    std::string displayName;
    std::string email;
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
    std::optional<std::string> assigneeUsername;
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
