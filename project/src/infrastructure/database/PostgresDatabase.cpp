#include "infrastructure/database/PostgresDatabase.h"

#include "common/FileUtil.h"
#include "common/Uuid.h"
#include "infrastructure/database/Migration.h"
#include "domain/Errors.h"

#include <libpq-fe.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TicketHub::Infrastructure::Database {
namespace {

struct ConnectionDeleter {
    void operator()(PGconn* connection) const { PQfinish(connection); }
};

struct ResultDeleter {
    void operator()(PGresult* result) const { PQclear(result); }
};

using ConnectionPtr = std::unique_ptr<PGconn, ConnectionDeleter>;
using ResultPtr = std::unique_ptr<PGresult, ResultDeleter>;

ConnectionPtr connect(const std::string& connectionString) {
    ConnectionPtr connection(PQconnectdb(connectionString.c_str()));
    if (!connection || PQstatus(connection.get()) != CONNECTION_OK) {
        const std::string message = connection ? PQerrorMessage(connection.get()) : "allocation failed";
        throw std::runtime_error("PostgreSQL connection failed: " + message);
    }
    return connection;
}

void requireStatus(PGconn* connection,
                   PGresult* result,
                   std::initializer_list<ExecStatusType> allowed,
                   const std::string& action) {
    if (result == nullptr) {
        throw std::runtime_error(action + " failed: " + PQerrorMessage(connection));
    }
    const auto status = PQresultStatus(result);
    for (const auto candidate : allowed) {
        if (status == candidate) {
            return;
        }
    }
    throw std::runtime_error(action + " failed: " + PQresultErrorMessage(result));
}

ResultPtr exec(PGconn* connection, const std::string& sql, const std::string& action) {
    ResultPtr result(PQexec(connection, sql.c_str()));
    requireStatus(connection, result.get(), {PGRES_COMMAND_OK, PGRES_TUPLES_OK}, action);
    return result;
}

ResultPtr execParams(PGconn* connection,
                     const std::string& sql,
                     const std::vector<std::optional<std::string>>& parameters,
                     const std::string& action) {
    std::vector<const char*> values;
    values.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        values.push_back(parameter ? parameter->c_str() : nullptr);
    }
    ResultPtr result(PQexecParams(connection,
                                  sql.c_str(),
                                  static_cast<int>(values.size()),
                                  nullptr,
                                  values.data(),
                                  nullptr,
                                  nullptr,
                                  0));
    requireStatus(connection, result.get(), {PGRES_COMMAND_OK, PGRES_TUPLES_OK}, action);
    return result;
}

std::string value(PGresult* result, int row, int column) {
    if (PQgetisnull(result, row, column) != 0) {
        return {};
    }
    return PQgetvalue(result, row, column);
}

std::optional<std::string> optionalValue(PGresult* result, int row, int column) {
    if (PQgetisnull(result, row, column) != 0) {
        return std::nullopt;
    }
    return value(result, row, column);
}

std::int64_t int64Value(PGresult* result, int row, int column) {
    const auto text = value(result, row, column);
    return text.empty() ? 0 : std::stoll(text);
}

int intValue(PGresult* result, int row, int column) {
    const auto text = value(result, row, column);
    return text.empty() ? 0 : std::stoi(text);
}

bool boolValue(PGresult* result, int row, int column) {
    return value(result, row, column) == "t";
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

Domain::UserSummary readUserSummary(PGresult* result, int row, int offset) {
    return Domain::UserSummary{value(result, row, offset), value(result, row, offset + 1), value(result, row, offset + 2)};
}

Domain::User readUser(PGresult* result, int row) {
    Domain::User user;
    user.id = value(result, row, 0);
    user.email = value(result, row, 1);
    user.displayName = value(result, row, 2);
    user.timeZone = value(result, row, 3);
    user.clockFormat = value(result, row, 4);
    user.active = boolValue(result, row, 5);
    user.isAdmin = boolValue(result, row, 6);
    user.createdAt = value(result, row, 7);
    return user;
}

constexpr const char* UserSelect =
    "SELECT id, email, display_name, time_zone, clock_format, active, is_admin, created_at::text FROM users";

Domain::Issue readIssue(PGresult* result, int row) {
    Domain::Issue issue;
    issue.id = value(result, row, 0);
    issue.key = value(result, row, 1);
    issue.number = int64Value(result, row, 2);
    issue.projectKey = value(result, row, 3);
    issue.projectName = value(result, row, 4);
    issue.summary = value(result, row, 5);
    issue.description = value(result, row, 6);
    issue.type = {value(result, row, 7), value(result, row, 8), value(result, row, 9), value(result, row, 10)};
    issue.status = {value(result, row, 11), value(result, row, 12), value(result, row, 13), intValue(result, row, 14)};
    issue.priority = {value(result, row, 15), value(result, row, 16), intValue(result, row, 17), value(result, row, 18)};
    issue.reporter = readUserSummary(result, row, 19);
    if (PQgetisnull(result, row, 22) == 0) {
        issue.assignee = readUserSummary(result, row, 22);
    }
    issue.parentIssueKey = optionalValue(result, row, 25);
    if (PQgetisnull(result, row, 26) == 0) {
        issue.storyPoints = std::stod(value(result, row, 26));
    }
    issue.dueDate = optionalValue(result, row, 27);
    issue.labels = splitLabels(value(result, row, 28));
    issue.createdAt = value(result, row, 29);
    issue.updatedAt = value(result, row, 30);
    issue.version = int64Value(result, row, 31);
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
    reporter.id, reporter.display_name, reporter.email,
    assignee.id, assignee.display_name, assignee.email,
    parent.issue_key,
    i.story_points, i.due_date::text,
    labels.names,
    i.created_at::text, i.updated_at::text, i.version
FROM issues i
JOIN projects p ON p.id = i.project_id
JOIN issue_types it ON it.id = i.issue_type_id
JOIN issue_statuses s ON s.id = i.status_id
JOIN priorities pr ON pr.id = i.priority_id
JOIN users reporter ON reporter.id = i.reporter_user_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
LEFT JOIN issues parent ON parent.id = i.parent_issue_id
LEFT JOIN LATERAL (
    SELECT COALESCE(string_agg(l.name, ',' ORDER BY l.name), '') AS names
    FROM issue_labels il JOIN labels l ON l.id = il.label_id
    WHERE il.issue_id = i.id
) labels ON TRUE
)SQL";

std::string lookupId(PGconn* connection,
                     const std::string& table,
                     const std::string& keyColumn,
                     const std::string& key) {
    auto result = execParams(connection,
                             "SELECT id FROM " + table + " WHERE " + keyColumn + " = $1",
                             {key},
                             "Lookup " + table);
    if (PQntuples(result.get()) != 1) {
        throw std::invalid_argument("Unknown " + table + " key: " + key);
    }
    return value(result.get(), 0, 0);
}

std::string requireUserId(PGconn* connection, const std::string& userId) {
    return lookupId(connection, "users", "id", userId);
}

std::string lookupIssueId(PGconn* connection, const std::string& issueKey) {
    auto result = execParams(connection, R"SQL(
SELECT i.id
FROM issues i
WHERE i.deleted_at IS NULL
  AND (i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1))
)SQL", {issueKey}, "Lookup issue");
    if (PQntuples(result.get()) != 1) {
        throw std::invalid_argument("Unknown issue key: " + issueKey);
    }
    return value(result.get(), 0, 0);
}

} // namespace

PostgresDatabase::PostgresDatabase(std::string connectionString,
                                   std::string migrationsDirectory,
                                   std::string seedPath)
    : connectionString_(std::move(connectionString)),
      migrationsDirectory_(std::move(migrationsDirectory)),
      seedPath_(std::move(seedPath)) {}

std::string PostgresDatabase::backendName() const {
    return "postgres";
}

void PostgresDatabase::migrate() {
    auto connection = connect(connectionString_);
    exec(connection.get(), "SELECT pg_advisory_lock(hashtext('ticket-hub-schema-migrations'))", "Lock migrations");
    try {
        exec(connection.get(), R"SQL(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version VARCHAR(128) PRIMARY KEY,
    checksum VARCHAR(64),
    applied_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
)
)SQL", "Create migration metadata");
        exec(connection.get(),
             "ALTER TABLE schema_migrations ADD COLUMN IF NOT EXISTS checksum VARCHAR(64)",
             "Ensure migration checksum column");

        for (const auto& migration : discoverMigrationFiles(migrationsDirectory_)) {
            const std::string sql = Common::readTextFile(migration.path.string());
            auto applied = execParams(connection.get(),
                                      "SELECT checksum FROM schema_migrations WHERE version = $1",
                                      {migration.version},
                                      "Read migration metadata");
            if (PQntuples(applied.get()) == 1) {
                const auto recorded = optionalValue(applied.get(), 0, 0);
                if (!recorded || recorded->empty()) {
                    execParams(connection.get(),
                               "UPDATE schema_migrations SET checksum = $1 WHERE version = $2",
                               {migration.checksum, migration.version},
                               "Adopt legacy migration checksum");
                } else if (*recorded != migration.checksum) {
                    throw std::runtime_error("Applied PostgreSQL migration was modified: " + migration.version);
                }
                continue;
            }

            exec(connection.get(), "BEGIN", "Begin migration");
            try {
                exec(connection.get(), sql, "Apply migration " + migration.version);
                execParams(connection.get(),
                           "INSERT INTO schema_migrations(version, checksum) VALUES ($1, $2)",
                           {migration.version, migration.checksum},
                           "Record migration " + migration.version);
                exec(connection.get(), "COMMIT", "Commit migration");
            } catch (...) {
                try {
                    exec(connection.get(), "ROLLBACK", "Rollback migration");
                } catch (...) {
                }
                throw;
            }
        }
        exec(connection.get(), "SELECT pg_advisory_unlock(hashtext('ticket-hub-schema-migrations'))", "Unlock migrations");
    } catch (...) {
        try {
            exec(connection.get(), "SELECT pg_advisory_unlock(hashtext('ticket-hub-schema-migrations'))", "Unlock migrations after failure");
        } catch (...) {
        }
        throw;
    }
}

void PostgresDatabase::seedDemoData() {
    auto connection = connect(connectionString_);
    exec(connection.get(), Common::readTextFile(seedPath_), "PostgreSQL demo seed");
}

// --- Identity ---

Domain::User PostgresDatabase::createUser(const Domain::CreateUserRequest& request,
                                          const std::string& passwordHash) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin create user transaction");
    try {
        const std::string userId = Common::uuidV4();
        auto existing = execParams(connection.get(), "SELECT 1 FROM users WHERE email = $1", {request.email},
                                   "Check existing email");
        if (PQntuples(existing.get()) != 0) {
            throw std::invalid_argument("Email is already in use: " + request.email);
        }

        execParams(connection.get(), R"SQL(
INSERT INTO users(id, email, display_name, is_admin, created_at, updated_at)
VALUES ($1, $2, $3, $4::boolean, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
                   {userId, request.email, request.displayName, request.isAdmin ? std::string("true") : std::string("false")},
                   "Insert user");

        execParams(connection.get(), R"SQL(
INSERT INTO local_credentials(user_id, password_hash, created_at, updated_at)
VALUES ($1, $2, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
                   {userId, passwordHash},
                   "Insert local credentials");

        exec(connection.get(), "COMMIT", "Commit create user transaction");

        auto result = execParams(connection.get(), std::string(UserSelect) + " WHERE id = $1", {userId}, "Read created user");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Created user could not be read back");
        }
        return readUser(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback create user transaction");
        } catch (...) {
        }
        throw;
    }
}

std::optional<Domain::User> PostgresDatabase::findUserByEmail(const std::string& email) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(UserSelect) + " WHERE email = $1", {email}, "Find user by email");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return readUser(result.get(), 0);
}

std::optional<Domain::User> PostgresDatabase::findUserById(const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(UserSelect) + " WHERE id = $1", {userId}, "Find user by id");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return readUser(result.get(), 0);
}

std::vector<Domain::User> PostgresDatabase::listUsers() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), std::string(UserSelect) + " ORDER BY display_name", "List users");
    std::vector<Domain::User> users;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        users.push_back(readUser(result.get(), row));
    }
    return users;
}

std::optional<std::string> PostgresDatabase::findPasswordHash(const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "SELECT password_hash FROM local_credentials WHERE user_id = $1",
                             {userId}, "Find password hash");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return value(result.get(), 0, 0);
}

void PostgresDatabase::recordFailedLogin(const std::string& userId) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), R"SQL(
UPDATE local_credentials
SET failed_login_count = failed_login_count + 1,
    locked_until = CASE
        WHEN failed_login_count + 1 >= $1::integer THEN CURRENT_TIMESTAMP + INTERVAL '15 minutes'
        ELSE locked_until
    END,
    updated_at = CURRENT_TIMESTAMP
WHERE user_id = $2
)SQL",
               {std::to_string(IDatabase::MaxFailedLoginAttempts), userId},
               "Record failed login");
}

void PostgresDatabase::resetFailedLogin(const std::string& userId) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), R"SQL(
UPDATE local_credentials
SET failed_login_count = 0, locked_until = NULL, updated_at = CURRENT_TIMESTAMP
WHERE user_id = $1
)SQL",
               {userId}, "Reset failed login");
}

bool PostgresDatabase::isLoginLocked(const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT 1 FROM local_credentials WHERE user_id = $1 AND locked_until IS NOT NULL AND locked_until > CURRENT_TIMESTAMP
)SQL",
                             {userId}, "Check login lock");
    return PQntuples(result.get()) > 0;
}

Domain::Session PostgresDatabase::createSession(const std::string& userId,
                                                const std::string& tokenHash,
                                                const std::string& expiresAtIso8601) {
    auto connection = connect(connectionString_);
    const std::string sessionId = Common::uuidV4();
    auto result = execParams(connection.get(), R"SQL(
INSERT INTO sessions(id, user_id, token_hash, created_at, expires_at)
VALUES ($1, $2, $3, CURRENT_TIMESTAMP, $4::timestamptz)
RETURNING id, user_id, created_at::text, expires_at::text
)SQL",
                             {sessionId, userId, tokenHash, expiresAtIso8601},
                             "Create session");
    if (PQntuples(result.get()) != 1) {
        throw std::runtime_error("Created session could not be read back");
    }
    return Domain::Session{
        value(result.get(), 0, 0), value(result.get(), 0, 1), value(result.get(), 0, 2), value(result.get(), 0, 3)};
}

std::optional<Domain::Session> PostgresDatabase::findSessionByTokenHash(const std::string& tokenHash) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT id, user_id, created_at::text, expires_at::text
FROM sessions
WHERE token_hash = $1 AND expires_at > CURRENT_TIMESTAMP
)SQL",
                             {tokenHash}, "Find session");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return Domain::Session{
        value(result.get(), 0, 0), value(result.get(), 0, 1), value(result.get(), 0, 2), value(result.get(), 0, 3)};
}

void PostgresDatabase::deleteSession(const std::string& sessionId) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), "DELETE FROM sessions WHERE id = $1", {sessionId}, "Delete session");
}

void PostgresDatabase::deleteExpiredSessions() {
    auto connection = connect(connectionString_);
    exec(connection.get(), "DELETE FROM sessions WHERE expires_at <= CURRENT_TIMESTAMP", "Delete expired sessions");
}

// --- Issue tracker ---

std::vector<Domain::Project> PostgresDatabase::listProjects() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), R"SQL(
SELECT p.id, p.project_key, p.name, p.description,
       lead.id, lead.display_name, lead.email,
       counts.issue_count, counts.open_issue_count
FROM projects p
LEFT JOIN users lead ON lead.id = p.lead_user_id
LEFT JOIN LATERAL (
    SELECT COUNT(i.id) AS issue_count,
           COUNT(i.id) FILTER (WHERE s.category <> 'done') AS open_issue_count
    FROM issues i JOIN issue_statuses s ON s.id = i.status_id
    WHERE i.project_id = p.id
) counts ON TRUE
WHERE p.archived = FALSE AND p.deleted_at IS NULL
ORDER BY p.name
)SQL", "List projects");

    std::vector<Domain::Project> projects;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        Domain::Project project;
        project.id = value(result.get(), row, 0);
        project.key = value(result.get(), row, 1);
        project.name = value(result.get(), row, 2);
        project.description = value(result.get(), row, 3);
        if (PQgetisnull(result.get(), row, 4) == 0) {
            project.lead = readUserSummary(result.get(), row, 4);
        }
        project.issueCount = int64Value(result.get(), row, 7);
        project.openIssueCount = int64Value(result.get(), row, 8);
        projects.push_back(std::move(project));
    }
    return projects;
}

std::vector<Domain::Issue> PostgresDatabase::listIssues(const Domain::IssueFilter& filter) {
    auto connection = connect(connectionString_);
    const std::string sql = std::string(IssueSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND ($1::text IS NULL OR p.project_key = $1)
  AND ($2::text IS NULL OR s.status_key = $2)
  AND ($3::text IS NULL OR i.summary ILIKE $3 OR i.issue_key ILIKE $3)
ORDER BY i.updated_at DESC, i.issue_key DESC
LIMIT 200
)SQL";
    auto result = execParams(connection.get(),
                             sql,
                             {filter.projectKey,
                              filter.statusKey,
                              filter.search ? std::optional<std::string>("%" + *filter.search + "%") : std::nullopt},
                             "List issues");
    std::vector<Domain::Issue> issues;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        issues.push_back(readIssue(result.get(), row));
    }
    return issues;
}

std::optional<Domain::Issue> PostgresDatabase::findIssueByKey(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(IssueSelect) + " WHERE i.deleted_at IS NULL AND (i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1))", {issueKey}, "Find issue");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return readIssue(result.get(), 0);
}

Domain::Issue PostgresDatabase::createIssue(const Domain::CreateIssueRequest& request,
                                            const std::string& reporterUserId) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin create issue transaction");
    try {
        auto project = execParams(connection.get(),
                                  "SELECT id, next_issue_number FROM projects WHERE project_key = $1 AND archived = FALSE AND deleted_at IS NULL FOR UPDATE",
                                  {request.projectKey},
                                  "Lock project");
        if (PQntuples(project.get()) != 1) {
            throw std::invalid_argument("Unknown project: " + request.projectKey);
        }
        const std::string projectId = value(project.get(), 0, 0);
        const std::int64_t issueNumber = int64Value(project.get(), 0, 1);
        const std::string issueKey = request.projectKey + "-" + std::to_string(issueNumber);

        execParams(connection.get(),
                   "UPDATE projects SET next_issue_number = next_issue_number + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $1",
                   {projectId},
                   "Increment project issue counter");

        const std::string issueId = Common::uuidV4();
        const std::string issueTypeId = lookupId(connection.get(), "issue_types", "type_key", request.issueTypeKey);
        const std::string statusId = lookupId(connection.get(), "issue_statuses", "status_key", "backlog");
        const std::string priorityId = lookupId(connection.get(), "priorities", "priority_key", request.priorityKey);
        const std::string reporterId = requireUserId(connection.get(), reporterUserId);
        std::optional<std::string> assigneeId;
        if (request.assigneeEmail && !request.assigneeEmail->empty()) {
            assigneeId = lookupId(connection.get(), "users", "email", *request.assigneeEmail);
        }

        execParams(connection.get(), R"SQL(
INSERT INTO issues(id, project_id, issue_number, issue_key, summary, description,
                   issue_type_id, status_id, priority_id, reporter_user_id, assignee_user_id,
                   story_points, due_date, created_at, updated_at)
VALUES ($1, $2, $3::bigint, $4, $5, $6, $7, $8, $9, $10, $11,
        $12::double precision, $13::date, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
                   {issueId,
                    projectId,
                    std::to_string(issueNumber),
                    issueKey,
                    request.summary,
                    request.description,
                    issueTypeId,
                    statusId,
                    priorityId,
                    reporterId,
                    assigneeId,
                    request.storyPoints ? std::optional<std::string>(std::to_string(*request.storyPoints)) : std::nullopt,
                    request.dueDate},
                   "Insert issue");

        for (const auto& labelName : request.labels) {
            execParams(connection.get(),
                       "INSERT INTO labels(id, name) VALUES ($1, $2) ON CONFLICT(name) DO NOTHING",
                       {Common::uuidV4(), labelName},
                       "Insert label");
            execParams(connection.get(), R"SQL(
INSERT INTO issue_labels(issue_id, label_id)
SELECT $1, id FROM labels WHERE name = $2
ON CONFLICT DO NOTHING
)SQL",
                       {issueId, labelName},
                       "Link label");
        }

        exec(connection.get(), "COMMIT", "Commit create issue transaction");
        auto result = execParams(connection.get(), std::string(IssueSelect) + " WHERE i.deleted_at IS NULL AND (i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1))", {issueKey}, "Read created issue");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Created issue could not be read back");
        }
        return readIssue(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback create issue transaction");
        } catch (...) {
        }
        throw;
    }
}

bool PostgresDatabase::changeIssueStatus(const std::string& issueKey,
                                         const std::string& statusKey,
                                         const std::string& actorUserId,
                                         const std::optional<std::int64_t> expectedVersion) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin status transaction");
    try {
        auto current = execParams(connection.get(), R"SQL(
SELECT i.id, s.status_key, i.version
FROM issues i JOIN issue_statuses s ON s.id = i.status_id
WHERE i.deleted_at IS NULL
  AND (i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1))
FOR UPDATE OF i
)SQL",
                                  {issueKey},
                                  "Lock issue");
        if (PQntuples(current.get()) == 0) {
            exec(connection.get(), "ROLLBACK", "Rollback missing issue transaction");
            return false;
        }
        const std::string issueId = value(current.get(), 0, 0);
        const std::string oldStatus = value(current.get(), 0, 1);
        const std::int64_t currentVersion = int64Value(current.get(), 0, 2);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Issue was modified by another user");
        }
        if (oldStatus == statusKey) {
            exec(connection.get(), "COMMIT", "Commit unchanged status transaction");
            return true;
        }
        const std::string statusId = lookupId(connection.get(), "issue_statuses", "status_key", statusKey);
        const std::string actorId = requireUserId(connection.get(), actorUserId);

        execParams(connection.get(),
                   "UPDATE issues SET status_id = $1, version = version + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $2",
                   {statusId, issueId},
                   "Update issue status");
        execParams(connection.get(), R"SQL(
INSERT INTO issue_history(id, issue_id, actor_user_id, field_name, old_value, new_value)
VALUES ($1, $2, $3, 'status', $4, $5)
)SQL",
                   {Common::uuidV4(), issueId, actorId, oldStatus, statusKey},
                   "Insert status history");
        exec(connection.get(), "COMMIT", "Commit status transaction");
        return true;
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback status transaction");
        } catch (...) {
        }
        throw;
    }
}

std::vector<Domain::Comment> PostgresDatabase::listComments(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT c.id, c.issue_id, u.id, u.display_name, u.email,
       c.body, c.created_at::text, c.updated_at::text
FROM comments c
JOIN issues i ON i.id = c.issue_id
JOIN users u ON u.id = c.author_user_id
WHERE c.deleted_at IS NULL
  AND i.deleted_at IS NULL
  AND (i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1))
ORDER BY c.created_at
)SQL",
                             {issueKey},
                             "List comments");
    std::vector<Domain::Comment> comments;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        comments.push_back(Domain::Comment{
            value(result.get(), row, 0),
            value(result.get(), row, 1),
            readUserSummary(result.get(), row, 2),
            value(result.get(), row, 5),
            value(result.get(), row, 6),
            value(result.get(), row, 7)});
    }
    return comments;
}

Domain::Comment PostgresDatabase::addComment(const Domain::AddCommentRequest& request,
                                             const std::string& authorUserId) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), request.issueKey);
    const std::string authorId = requireUserId(connection.get(), authorUserId);
    const std::string commentId = Common::uuidV4();
    auto result = execParams(connection.get(), R"SQL(
INSERT INTO comments(id, issue_id, author_user_id, body, created_at, updated_at)
VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
RETURNING created_at::text, updated_at::text
)SQL",
                             {commentId, issueId, authorId, request.body},
                             "Insert comment");
    auto author = execParams(connection.get(),
                             "SELECT id, display_name, email FROM users WHERE id = $1",
                             {authorId},
                             "Read comment author");
    return Domain::Comment{
        commentId,
        issueId,
        readUserSummary(author.get(), 0, 0),
        request.body,
        value(result.get(), 0, 0),
        value(result.get(), 0, 1)};
}

Domain::DashboardStats PostgresDatabase::dashboardStats() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), R"SQL(
SELECT COUNT(i.id),
       COUNT(i.id) FILTER (WHERE s.category = 'todo'),
       COUNT(i.id) FILTER (WHERE s.category = 'in_progress'),
       COUNT(i.id) FILTER (WHERE s.category = 'done')
FROM issues i JOIN issue_statuses s ON s.id = i.status_id
WHERE i.deleted_at IS NULL
)SQL",
                       "Dashboard statistics");
    Domain::DashboardStats stats;
    stats.totalIssues = int64Value(result.get(), 0, 0);
    stats.todoIssues = int64Value(result.get(), 0, 1);
    stats.inProgressIssues = int64Value(result.get(), 0, 2);
    stats.doneIssues = int64Value(result.get(), 0, 3);
    auto recent = listIssues({});
    if (recent.size() > 8) {
        recent.resize(8);
    }
    stats.recentIssues = std::move(recent);
    return stats;
}

} // namespace TicketHub::Infrastructure::Database
