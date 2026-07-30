#include "infrastructure/database/SqliteDatabase.h"

#include "common/FileUtil.h"
#include "common/Uuid.h"
#include "infrastructure/database/Migration.h"
#include "domain/Errors.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace TicketHub::Infrastructure::Database {
namespace {

class Statement {
public:
    Statement(sqlite3* database, const std::string& sql) {
        if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("SQLite prepare failed: ") + sqlite3_errmsg(database));
        }
    }

    ~Statement() {
        sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const { return statement_; }

    void bind(int index, const std::string& value) {
        if (sqlite3_bind_text(statement_, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
            throw std::runtime_error("SQLite text bind failed");
        }
    }

    void bind(int index, std::int64_t value) {
        if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK) {
            throw std::runtime_error("SQLite integer bind failed");
        }
    }

    void bind(int index, double value) {
        if (sqlite3_bind_double(statement_, index, value) != SQLITE_OK) {
            throw std::runtime_error("SQLite double bind failed");
        }
    }

    void bindNull(int index) {
        if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
            throw std::runtime_error("SQLite null bind failed");
        }
    }

    int step() { return sqlite3_step(statement_); }

private:
    sqlite3_stmt* statement_{};
};

std::string text(sqlite3_stmt* statement, int column) {
    const auto* value = sqlite3_column_text(statement, column);
    return value == nullptr ? std::string{} : reinterpret_cast<const char*>(value);
}

std::optional<std::string> optionalText(sqlite3_stmt* statement, int column) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    return text(statement, column);
}

std::vector<std::string> splitLabels(const std::string& labels) {
    std::vector<std::string> result;
    std::istringstream stream(labels);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

Domain::UserSummary readUser(sqlite3_stmt* statement, int offset) {
    return Domain::UserSummary{
        text(statement, offset),
        text(statement, offset + 1),
        text(statement, offset + 2),
        text(statement, offset + 3)};
}

Domain::Issue readIssue(sqlite3_stmt* statement) {
    Domain::Issue issue;
    issue.id = text(statement, 0);
    issue.key = text(statement, 1);
    issue.number = sqlite3_column_int64(statement, 2);
    issue.projectKey = text(statement, 3);
    issue.projectName = text(statement, 4);
    issue.summary = text(statement, 5);
    issue.description = text(statement, 6);
    issue.type = {text(statement, 7), text(statement, 8), text(statement, 9), text(statement, 10)};
    issue.status = {text(statement, 11), text(statement, 12), text(statement, 13), sqlite3_column_int(statement, 14)};
    issue.priority = {text(statement, 15), text(statement, 16), sqlite3_column_int(statement, 17), text(statement, 18)};
    issue.reporter = readUser(statement, 19);
    if (sqlite3_column_type(statement, 23) != SQLITE_NULL) {
        issue.assignee = readUser(statement, 23);
    }
    issue.parentIssueKey = optionalText(statement, 27);
    if (sqlite3_column_type(statement, 28) != SQLITE_NULL) {
        issue.storyPoints = sqlite3_column_double(statement, 28);
    }
    issue.dueDate = optionalText(statement, 29);
    issue.labels = splitLabels(text(statement, 30));
    issue.createdAt = text(statement, 31);
    issue.updatedAt = text(statement, 32);
    issue.version = sqlite3_column_int64(statement, 33);
    return issue;
}

constexpr const char* IssueSelect = R"SQL(
SELECT
    i.id, i.issue_key, i.issue_number,
    p.project_key, p.name,
    i.summary, i.description,
    it.type_key, it.name, it.icon, it.color,
    s.status_key, s.name, s.category, s.sort_order,
    pr.priority_key, pr.name, pr.rank, pr.color,
    reporter.id, reporter.username, reporter.display_name, reporter.email,
    assignee.id, assignee.username, assignee.display_name, assignee.email,
    parent.issue_key,
    i.story_points, i.due_date,
    COALESCE(GROUP_CONCAT(DISTINCT l.name), ''),
    i.created_at, i.updated_at, i.version
FROM issues i
JOIN projects p ON p.id = i.project_id
JOIN issue_types it ON it.id = i.issue_type_id
JOIN issue_statuses s ON s.id = i.status_id
JOIN priorities pr ON pr.id = i.priority_id
JOIN users reporter ON reporter.id = i.reporter_user_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
LEFT JOIN issues parent ON parent.id = i.parent_issue_id
LEFT JOIN issue_labels il ON il.issue_id = i.id
LEFT JOIN labels l ON l.id = il.label_id
)SQL";

std::string lookupId(sqlite3* database,
                     const std::string& table,
                     const std::string& keyColumn,
                     const std::string& key) {
    Statement statement(database, "SELECT id FROM " + table + " WHERE " + keyColumn + " = ?");
    statement.bind(1, key);
    if (statement.step() != SQLITE_ROW) {
        throw std::invalid_argument("Unknown " + table + " key: " + key);
    }
    return text(statement.get(), 0);
}

std::string lookupIssueId(sqlite3* database, const std::string& issueKey) {
    Statement statement(database, R"SQL(
SELECT i.id
FROM issues i
WHERE i.deleted_at IS NULL
  AND (i.issue_key = ?1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = ?1))
)SQL");
    statement.bind(1, issueKey);
    if (statement.step() != SQLITE_ROW) {
        throw std::invalid_argument("Unknown issue key: " + issueKey);
    }
    return text(statement.get(), 0);
}

void expectDone(sqlite3* database, Statement& statement, const std::string& action) {
    if (statement.step() != SQLITE_DONE) {
        throw std::runtime_error(action + " failed: " + sqlite3_errmsg(database));
    }
}

} // namespace

SqliteDatabase::SqliteDatabase(std::string databasePath, std::string migrationsDirectory, std::string seedPath)
    : migrationsDirectory_(std::move(migrationsDirectory)), seedPath_(std::move(seedPath)) {
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(databasePath.c_str(), &database_, flags, nullptr) != SQLITE_OK) {
        const std::string message = database_ == nullptr ? "unknown error" : sqlite3_errmsg(database_);
        if (database_ != nullptr) {
            sqlite3_close(database_);
            database_ = nullptr;
        }
        throw std::runtime_error("Cannot open SQLite database: " + message);
    }
    sqlite3_busy_timeout(database_, 5000);
    executeScript("PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;");
}

SqliteDatabase::~SqliteDatabase() {
    if (database_ != nullptr) {
        sqlite3_close(database_);
    }
}

std::string SqliteDatabase::backendName() const {
    return "sqlite";
}

void SqliteDatabase::executeScript(const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(database_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error == nullptr ? sqlite3_errmsg(database_) : error;
        sqlite3_free(error);
        throw std::runtime_error("SQLite script failed: " + message);
    }
}

void SqliteDatabase::migrate() {
    std::scoped_lock lock(mutex_);
    executeScript(R"SQL(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version TEXT PRIMARY KEY,
    checksum TEXT,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)SQL");

    bool hasChecksum = false;
    {
        Statement columns(database_, "PRAGMA table_info(schema_migrations)");
        for (int result = columns.step(); result == SQLITE_ROW; result = columns.step()) {
            if (text(columns.get(), 1) == "checksum") {
                hasChecksum = true;
                break;
            }
        }
    }
    if (!hasChecksum) {
        executeScript("ALTER TABLE schema_migrations ADD COLUMN checksum TEXT;");
    }

    for (const auto& migration : discoverMigrationFiles(migrationsDirectory_)) {
        const std::string sql = Common::readTextFile(migration.path.string());
        Statement applied(database_, "SELECT checksum FROM schema_migrations WHERE version = ?");
        applied.bind(1, migration.version);
        if (applied.step() == SQLITE_ROW) {
            const auto recorded = optionalText(applied.get(), 0);
            if (!recorded || recorded->empty()) {
                Statement adopt(database_, "UPDATE schema_migrations SET checksum = ? WHERE version = ?");
                adopt.bind(1, migration.checksum);
                adopt.bind(2, migration.version);
                expectDone(database_, adopt, "Adopt legacy migration checksum");
            } else if (*recorded != migration.checksum) {
                throw std::runtime_error("Applied SQLite migration was modified: " + migration.version);
            }
            continue;
        }

        executeScript("BEGIN IMMEDIATE;");
        try {
            executeScript(sql);
            Statement record(database_,
                             "INSERT INTO schema_migrations(version, checksum) VALUES (?, ?)");
            record.bind(1, migration.version);
            record.bind(2, migration.checksum);
            expectDone(database_, record, "Record SQLite migration");
            executeScript("COMMIT;");
        } catch (...) {
            try {
                executeScript("ROLLBACK;");
            } catch (...) {
            }
            throw;
        }
    }
}

void SqliteDatabase::seedDemoData() {
    std::scoped_lock lock(mutex_);
    executeScript(Common::readTextFile(seedPath_));
}

std::vector<Domain::Project> SqliteDatabase::listProjects() {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT p.id, p.project_key, p.name, p.description,
       lead.id, lead.username, lead.display_name, lead.email,
       COUNT(i.id),
       SUM(CASE WHEN s.category <> 'done' THEN 1 ELSE 0 END)
FROM projects p
LEFT JOIN users lead ON lead.id = p.lead_user_id
LEFT JOIN issues i ON i.project_id = p.id AND i.deleted_at IS NULL
LEFT JOIN issue_statuses s ON s.id = i.status_id
WHERE p.archived = 0 AND p.deleted_at IS NULL
GROUP BY p.id
ORDER BY p.name
)SQL");

    std::vector<Domain::Project> projects;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        Domain::Project project;
        project.id = text(statement.get(), 0);
        project.key = text(statement.get(), 1);
        project.name = text(statement.get(), 2);
        project.description = text(statement.get(), 3);
        if (sqlite3_column_type(statement.get(), 4) != SQLITE_NULL) {
            project.lead = readUser(statement.get(), 4);
        }
        project.issueCount = sqlite3_column_int64(statement.get(), 8);
        project.openIssueCount = sqlite3_column_int64(statement.get(), 9);
        projects.push_back(std::move(project));
    }
    return projects;
}

std::vector<Domain::Issue> SqliteDatabase::listIssues(const Domain::IssueFilter& filter) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(IssueSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND (?1 IS NULL OR p.project_key = ?1)
  AND (?2 IS NULL OR s.status_key = ?2)
  AND (?3 IS NULL OR LOWER(i.summary) LIKE LOWER(?3) OR LOWER(i.issue_key) LIKE LOWER(?3))
GROUP BY i.id
ORDER BY i.updated_at DESC, i.issue_key DESC
LIMIT 200
)SQL";
    Statement statement(database_, sql);
    filter.projectKey ? statement.bind(1, *filter.projectKey) : statement.bindNull(1);
    filter.statusKey ? statement.bind(2, *filter.statusKey) : statement.bindNull(2);
    filter.search ? statement.bind(3, "%" + *filter.search + "%") : statement.bindNull(3);

    std::vector<Domain::Issue> issues;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        issues.push_back(readIssue(statement.get()));
    }
    return issues;
}

std::optional<Domain::Issue> SqliteDatabase::findIssueByKey(const std::string& issueKey) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(IssueSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND (i.issue_key = ?1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = ?1))
GROUP BY i.id
)SQL";
    Statement statement(database_, sql);
    statement.bind(1, issueKey);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readIssue(statement.get());
}

Domain::Issue SqliteDatabase::createIssue(const Domain::CreateIssueRequest& request,
                                          const std::string& reporterUsername) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement projectStatement(database_, "SELECT id, next_issue_number FROM projects WHERE project_key = ? AND archived = 0 AND deleted_at IS NULL");
        projectStatement.bind(1, request.projectKey);
        if (projectStatement.step() != SQLITE_ROW) {
            throw std::invalid_argument("Unknown project: " + request.projectKey);
        }
        const std::string projectId = text(projectStatement.get(), 0);
        const std::int64_t issueNumber = sqlite3_column_int64(projectStatement.get(), 1);
        const std::string issueKey = request.projectKey + "-" + std::to_string(issueNumber);

        Statement increment(database_, "UPDATE projects SET next_issue_number = next_issue_number + 1, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
        increment.bind(1, projectId);
        expectDone(database_, increment, "Project counter update");

        const std::string issueId = Common::uuidV4();
        const std::string issueTypeId = lookupId(database_, "issue_types", "type_key", request.issueTypeKey);
        const std::string statusId = lookupId(database_, "issue_statuses", "status_key", "backlog");
        const std::string priorityId = lookupId(database_, "priorities", "priority_key", request.priorityKey);
        const std::string reporterId = lookupId(database_, "users", "username", reporterUsername);
        std::optional<std::string> assigneeId;
        if (request.assigneeUsername.has_value() && !request.assigneeUsername->empty()) {
            assigneeId = lookupId(database_, "users", "username", *request.assigneeUsername);
        }

        Statement insert(database_, R"SQL(
INSERT INTO issues(id, project_id, issue_number, issue_key, summary, description,
                   issue_type_id, status_id, priority_id, reporter_user_id, assignee_user_id,
                   story_points, due_date, created_at, updated_at)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
        insert.bind(1, issueId);
        insert.bind(2, projectId);
        insert.bind(3, issueNumber);
        insert.bind(4, issueKey);
        insert.bind(5, request.summary);
        insert.bind(6, request.description);
        insert.bind(7, issueTypeId);
        insert.bind(8, statusId);
        insert.bind(9, priorityId);
        insert.bind(10, reporterId);
        assigneeId ? insert.bind(11, *assigneeId) : insert.bindNull(11);
        request.storyPoints ? insert.bind(12, *request.storyPoints) : insert.bindNull(12);
        request.dueDate ? insert.bind(13, *request.dueDate) : insert.bindNull(13);
        expectDone(database_, insert, "Issue insert");

        for (const auto& labelName : request.labels) {
            const std::string labelId = Common::uuidV4();
            Statement label(database_, "INSERT INTO labels(id, name) VALUES (?, ?) ON CONFLICT(name) DO NOTHING");
            label.bind(1, labelId);
            label.bind(2, labelName);
            expectDone(database_, label, "Label insert");

            Statement link(database_, R"SQL(
INSERT OR IGNORE INTO issue_labels(issue_id, label_id)
SELECT ?, id FROM labels WHERE name = ?
)SQL");
            link.bind(1, issueId);
            link.bind(2, labelName);
            expectDone(database_, link, "Issue label insert");
        }

        executeScript("COMMIT;");
        const std::string sql = std::string(IssueSelect) + " WHERE i.deleted_at IS NULL AND i.issue_key = ?1 GROUP BY i.id";
        Statement read(database_, sql);
        read.bind(1, issueKey);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Created issue could not be read back");
        }
        return readIssue(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SqliteDatabase::changeIssueStatus(const std::string& issueKey,
                                       const std::string& statusKey,
                                       const std::string& actorUsername,
                                       const std::optional<std::int64_t> expectedVersion) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement current(database_, R"SQL(
SELECT i.id, s.status_key, i.version
FROM issues i JOIN issue_statuses s ON s.id = i.status_id
WHERE i.deleted_at IS NULL
  AND (i.issue_key = ?1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = ?1))
)SQL");
        current.bind(1, issueKey);
        if (current.step() != SQLITE_ROW) {
            executeScript("ROLLBACK;");
            return false;
        }
        const std::string issueId = text(current.get(), 0);
        const std::string oldStatus = text(current.get(), 1);
        const std::int64_t currentVersion = sqlite3_column_int64(current.get(), 2);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Issue was modified by another user");
        }
        if (oldStatus == statusKey) {
            executeScript("COMMIT;");
            return true;
        }
        const std::string statusId = lookupId(database_, "issue_statuses", "status_key", statusKey);
        const std::string actorId = lookupId(database_, "users", "username", actorUsername);

        Statement update(database_, "UPDATE issues SET status_id = ?, version = version + 1, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
        update.bind(1, statusId);
        update.bind(2, issueId);
        expectDone(database_, update, "Issue status update");

        Statement history(database_, R"SQL(
INSERT INTO issue_history(id, issue_id, actor_user_id, field_name, old_value, new_value)
VALUES (?, ?, ?, 'status', ?, ?)
)SQL");
        history.bind(1, Common::uuidV4());
        history.bind(2, issueId);
        history.bind(3, actorId);
        history.bind(4, oldStatus);
        history.bind(5, statusKey);
        expectDone(database_, history, "Issue history insert");
        executeScript("COMMIT;");
        return true;
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

std::vector<Domain::Comment> SqliteDatabase::listComments(const std::string& issueKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT c.id, c.issue_id, u.id, u.username, u.display_name, u.email,
       c.body, c.created_at, c.updated_at
FROM comments c
JOIN issues i ON i.id = c.issue_id
JOIN users u ON u.id = c.author_user_id
WHERE c.deleted_at IS NULL
  AND i.deleted_at IS NULL
  AND (i.issue_key = ?1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = ?1))
ORDER BY c.created_at
)SQL");
    statement.bind(1, issueKey);
    std::vector<Domain::Comment> comments;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        comments.push_back(Domain::Comment{
            text(statement.get(), 0),
            text(statement.get(), 1),
            readUser(statement.get(), 2),
            text(statement.get(), 6),
            text(statement.get(), 7),
            text(statement.get(), 8)});
    }
    return comments;
}

Domain::Comment SqliteDatabase::addComment(const Domain::AddCommentRequest& request,
                                           const std::string& authorUsername) {
    std::scoped_lock lock(mutex_);
    const std::string issueId = lookupIssueId(database_, request.issueKey);
    const std::string authorId = lookupId(database_, "users", "username", authorUsername);
    const std::string commentId = Common::uuidV4();

    Statement insert(database_, R"SQL(
INSERT INTO comments(id, issue_id, author_user_id, body, created_at, updated_at)
VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, commentId);
    insert.bind(2, issueId);
    insert.bind(3, authorId);
    insert.bind(4, request.body);
    expectDone(database_, insert, "Comment insert");

    Statement read(database_, R"SQL(
SELECT c.id, c.issue_id, u.id, u.username, u.display_name, u.email,
       c.body, c.created_at, c.updated_at
FROM comments c JOIN users u ON u.id = c.author_user_id
WHERE c.id = ?
)SQL");
    read.bind(1, commentId);
    if (read.step() != SQLITE_ROW) {
        throw std::runtime_error("Created comment could not be read back");
    }
    return Domain::Comment{
        text(read.get(), 0), text(read.get(), 1), readUser(read.get(), 2), text(read.get(), 6),
        text(read.get(), 7), text(read.get(), 8)};
}

Domain::DashboardStats SqliteDatabase::dashboardStats() {
    Domain::DashboardStats stats;
    {
        std::scoped_lock lock(mutex_);
        Statement statement(database_, R"SQL(
SELECT COUNT(i.id),
       SUM(CASE WHEN s.category = 'todo' THEN 1 ELSE 0 END),
       SUM(CASE WHEN s.category = 'in_progress' THEN 1 ELSE 0 END),
       SUM(CASE WHEN s.category = 'done' THEN 1 ELSE 0 END)
FROM issues i JOIN issue_statuses s ON s.id = i.status_id
WHERE i.deleted_at IS NULL
)SQL");
        if (statement.step() == SQLITE_ROW) {
            stats.totalIssues = sqlite3_column_int64(statement.get(), 0);
            stats.todoIssues = sqlite3_column_int64(statement.get(), 1);
            stats.inProgressIssues = sqlite3_column_int64(statement.get(), 2);
            stats.doneIssues = sqlite3_column_int64(statement.get(), 3);
        }
    }
    auto recent = listIssues({});
    if (recent.size() > 8) {
        recent.resize(8);
    }
    stats.recentIssues = std::move(recent);
    return stats;
}

} // namespace TicketHub::Infrastructure::Database
