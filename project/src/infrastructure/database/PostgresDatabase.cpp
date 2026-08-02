#include "infrastructure/database/PostgresDatabase.h"

#include "common/FileUtil.h"
#include "common/Uuid.h"
#include "infrastructure/database/Migration.h"
#include "domain/Errors.h"

#include <libpq-fe.h>

#include <algorithm>
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
    user.handle = optionalValue(result, row, 3);
    user.timeZone = value(result, row, 4);
    user.clockFormat = value(result, row, 5);
    user.active = boolValue(result, row, 6);
    user.isAdmin = boolValue(result, row, 7);
    user.createdAt = value(result, row, 8);
    return user;
}

constexpr const char* UserSelect =
    "SELECT id, email, display_name, handle, time_zone, clock_format, active, is_admin, created_at::text FROM users";

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
    issue.resolution = optionalValue(result, row, 32);
    issue.rankOrder = int64Value(result, row, 33);
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
    i.created_at::text, i.updated_at::text, i.version, i.resolution, i.rank_order
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

Domain::Comment readComment(PGresult* result, int row) {
    Domain::Comment comment;
    comment.id = value(result, row, 0);
    comment.issueId = value(result, row, 1);
    comment.author = readUserSummary(result, row, 2);
    comment.body = value(result, row, 5);
    comment.createdAt = value(result, row, 6);
    comment.updatedAt = value(result, row, 7);
    comment.version = int64Value(result, row, 8);
    comment.editedAt = optionalValue(result, row, 9);
    return comment;
}

constexpr const char* CommentSelect = R"SQL(
SELECT c.id, c.issue_id, u.id, u.display_name, u.email,
       c.body, c.created_at::text, c.updated_at::text, c.version, c.edited_at::text
FROM comments c JOIN users u ON u.id = c.author_user_id
)SQL";

Domain::Worklog readWorklog(PGresult* result, int row) {
    Domain::Worklog worklog;
    worklog.id = value(result, row, 0);
    worklog.issueId = value(result, row, 1);
    worklog.author = readUserSummary(result, row, 2);
    worklog.workDate = value(result, row, 5);
    worklog.timeSpentSeconds = int64Value(result, row, 6);
    worklog.comment = optionalValue(result, row, 7);
    worklog.createdAt = value(result, row, 8);
    worklog.updatedAt = value(result, row, 9);
    worklog.version = int64Value(result, row, 10);
    return worklog;
}

constexpr const char* WorklogSelect = R"SQL(
SELECT w.id, w.issue_id, u.id, u.display_name, u.email,
       w.work_date::text, w.time_spent_seconds, w.comment, w.created_at::text, w.updated_at::text, w.version
FROM worklogs w JOIN users u ON u.id = w.author_user_id
)SQL";

Domain::Attachment readAttachment(PGresult* result, int row) {
    Domain::Attachment attachment;
    attachment.id = value(result, row, 0);
    attachment.issueId = value(result, row, 1);
    attachment.uploader = readUserSummary(result, row, 2);
    attachment.fileName = value(result, row, 5);
    attachment.contentType = value(result, row, 6);
    attachment.byteSize = int64Value(result, row, 7);
    attachment.sha256 = value(result, row, 8);
    attachment.createdAt = value(result, row, 9);
    attachment.deletedAt = optionalValue(result, row, 10);
    attachment.issueKey = value(result, row, 11);
    return attachment;
}

constexpr const char* AttachmentSelect = R"SQL(
SELECT a.id, a.issue_id, u.id, u.display_name, u.email,
       a.file_name, a.content_type, a.byte_size, a.sha256, a.created_at::text, a.deleted_at::text, i.issue_key
FROM attachments a JOIN users u ON u.id = a.uploader_user_id JOIN issues i ON i.id = a.issue_id
)SQL";

Domain::AuditEvent readAuditEvent(PGresult* result, int row) {
    Domain::AuditEvent event;
    event.id = value(result, row, 0);
    event.category = value(result, row, 1);
    event.action = value(result, row, 2);
    if (PQgetisnull(result, row, 3) == 0) {
        event.actor = readUserSummary(result, row, 3);
    }
    event.targetType = optionalValue(result, row, 6);
    event.targetId = optionalValue(result, row, 7);
    event.details = optionalValue(result, row, 8);
    event.createdAt = value(result, row, 9);
    return event;
}

// LEFT JOIN, not JOIN: actor_user_id may be NULL (a failed/blocked login
// attempt, or CLI-driven account creation, has no authenticated actor).
constexpr const char* AuditEventSelect = R"SQL(
SELECT a.id, a.category, a.action, u.id, u.display_name, u.email,
       a.target_type, a.target_id, a.details, a.created_at::text
FROM audit_events a LEFT JOIN users u ON u.id = a.actor_user_id
)SQL";

Domain::BoardColumn readBoardColumn(PGresult* result, int row) {
    Domain::BoardColumn column;
    column.id = value(result, row, 0);
    column.statusKey = value(result, row, 1);
    column.statusName = value(result, row, 2);
    column.sortOrder = std::stoi(value(result, row, 3));
    if (PQgetisnull(result, row, 4) == 0) {
        column.wipLimit = std::stoi(value(result, row, 4));
    }
    return column;
}

constexpr const char* BoardColumnSelect = R"SQL(
SELECT bc.id, s.status_key, s.name, bc.sort_order, bc.wip_limit
FROM board_columns bc JOIN issue_statuses s ON s.id = bc.status_id
ORDER BY bc.sort_order
)SQL";

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
        if (request.handle) {
            auto existingHandle = execParams(connection.get(), "SELECT 1 FROM users WHERE handle = $1", {*request.handle},
                                             "Check existing handle");
            if (PQntuples(existingHandle.get()) != 0) {
                throw std::invalid_argument("Handle is already in use: " + *request.handle);
            }
        }

        execParams(connection.get(), R"SQL(
INSERT INTO users(id, email, display_name, handle, is_admin, created_at, updated_at)
VALUES ($1, $2, $3, $4, $5::boolean, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
                   {userId, request.email, request.displayName, request.handle,
                    request.isAdmin ? std::string("true") : std::string("false")},
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

std::optional<Domain::User> PostgresDatabase::findUserByHandle(const std::string& handle) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(UserSelect) + " WHERE handle = $1", {handle}, "Find user by handle");
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

std::vector<Domain::Session> PostgresDatabase::listSessionsForUser(const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT id, user_id, created_at::text, expires_at::text FROM sessions
WHERE user_id = $1 AND expires_at > CURRENT_TIMESTAMP
ORDER BY created_at DESC
)SQL",
                             {userId}, "List sessions for user");
    std::vector<Domain::Session> sessions;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        sessions.push_back(Domain::Session{
            value(result.get(), row, 0), value(result.get(), row, 1), value(result.get(), row, 2), value(result.get(), row, 3)});
    }
    return sessions;
}

int PostgresDatabase::deleteOtherSessionsForUser(const std::string& userId, const std::string& keepSessionId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "DELETE FROM sessions WHERE user_id = $1 AND id <> $2",
                             {userId, keepSessionId}, "Delete other sessions for user");
    return std::stoi(PQcmdTuples(result.get()));
}

namespace {
Domain::PersonalAccessToken readPersonalAccessToken(PGresult* result, int row) {
    Domain::PersonalAccessToken token;
    token.id = value(result, row, 0);
    token.userId = value(result, row, 1);
    token.name = value(result, row, 2);
    token.createdAt = value(result, row, 3);
    token.expiresAt = value(result, row, 4);
    token.lastUsedAt = optionalValue(result, row, 5);
    token.revokedAt = optionalValue(result, row, 6);
    return token;
}
constexpr const char* PersonalAccessTokenSelect =
    "SELECT id, user_id, name, created_at::text, expires_at::text, last_used_at::text, revoked_at::text "
    "FROM personal_access_tokens";
} // namespace

Domain::PersonalAccessToken PostgresDatabase::createPersonalAccessToken(const std::string& userId,
                                                                        const std::string& name,
                                                                        const std::string& tokenHash,
                                                                        const std::string& expiresAtIso8601) {
    auto connection = connect(connectionString_);
    const std::string tokenId = Common::uuidV4();
    execParams(connection.get(), R"SQL(
INSERT INTO personal_access_tokens(id, user_id, name, token_hash, created_at, expires_at)
VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP, $5::timestamptz)
)SQL",
              {tokenId, userId, name, tokenHash, expiresAtIso8601}, "Create personal access token");

    auto result = execParams(connection.get(), std::string(PersonalAccessTokenSelect) + " WHERE id = $1",
                             {tokenId}, "Read created personal access token");
    if (PQntuples(result.get()) != 1) {
        throw std::runtime_error("Created personal access token could not be read back");
    }
    return readPersonalAccessToken(result.get(), 0);
}

std::optional<Domain::PersonalAccessToken> PostgresDatabase::findPersonalAccessTokenByHash(const std::string& tokenHash) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(PersonalAccessTokenSelect)
        + " WHERE token_hash = $1 AND expires_at > CURRENT_TIMESTAMP AND revoked_at IS NULL",
                             {tokenHash}, "Find personal access token");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return readPersonalAccessToken(result.get(), 0);
}

std::vector<Domain::PersonalAccessToken> PostgresDatabase::listPersonalAccessTokens(const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(PersonalAccessTokenSelect) + " WHERE user_id = $1 ORDER BY created_at DESC",
                             {userId}, "List personal access tokens");
    std::vector<Domain::PersonalAccessToken> tokens;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        tokens.push_back(readPersonalAccessToken(result.get(), row));
    }
    return tokens;
}

bool PostgresDatabase::revokePersonalAccessToken(const std::string& tokenId, const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE personal_access_tokens SET revoked_at = CURRENT_TIMESTAMP
WHERE id = $1 AND user_id = $2 AND revoked_at IS NULL
)SQL",
                             {tokenId, userId}, "Revoke personal access token");
    return std::string(PQcmdTuples(result.get())) != "0";
}

void PostgresDatabase::touchPersonalAccessTokenLastUsed(const std::string& tokenId) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), "UPDATE personal_access_tokens SET last_used_at = CURRENT_TIMESTAMP WHERE id = $1",
              {tokenId}, "Touch personal access token last used");
}

// --- Authorization and project lifecycle ---

namespace {
constexpr const char* ProjectSelectSql = R"SQL(
SELECT p.id, p.project_key, p.name, p.description,
       lead.id, lead.display_name, lead.email,
       counts.issue_count, counts.open_issue_count,
       p.archived
FROM projects p
LEFT JOIN users lead ON lead.id = p.lead_user_id
LEFT JOIN LATERAL (
    SELECT COUNT(i.id) AS issue_count,
           COUNT(i.id) FILTER (WHERE s.category <> 'done') AS open_issue_count
    FROM issues i JOIN issue_statuses s ON s.id = i.status_id
    WHERE i.project_id = p.id AND i.deleted_at IS NULL
) counts ON TRUE
)SQL";

Domain::Project readProject(PGresult* result, int row) {
    Domain::Project project;
    project.id = value(result, row, 0);
    project.key = value(result, row, 1);
    project.name = value(result, row, 2);
    project.description = value(result, row, 3);
    if (PQgetisnull(result, row, 4) == 0) {
        project.lead = readUserSummary(result, row, 4);
    }
    project.issueCount = int64Value(result, row, 7);
    project.openIssueCount = int64Value(result, row, 8);
    project.archived = boolValue(result, row, 9);
    return project;
}
} // namespace

std::optional<std::string> PostgresDatabase::findProjectRoleByKey(const std::string& projectKey,
                                                                   const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT pm.role_key
FROM project_members pm
JOIN projects p ON p.id = pm.project_id
WHERE p.project_key = $1 AND pm.user_id = $2
)SQL",
                             {projectKey, userId}, "Find project role");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return value(result.get(), 0, 0);
}

Domain::Project PostgresDatabase::createProject(const Domain::CreateProjectRequest& request,
                                                const std::string& creatorUserId) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin create project transaction");
    try {
        const std::string projectId = Common::uuidV4();
        auto existing = execParams(connection.get(), "SELECT 1 FROM projects WHERE project_key = $1",
                                   {request.key}, "Check existing project key");
        if (PQntuples(existing.get()) != 0) {
            throw std::invalid_argument("Project key is already in use: " + request.key);
        }

        execParams(connection.get(), R"SQL(
INSERT INTO projects(id, project_key, name, description, lead_user_id, next_issue_number, archived, created_at, updated_at)
VALUES ($1, $2, $3, $4, $5, 1, FALSE, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
                   {projectId, request.key, request.name, request.description, creatorUserId},
                   "Insert project");

        execParams(connection.get(),
                   "INSERT INTO project_members(project_id, user_id, role_key) VALUES ($1, $2, $3)",
                   {projectId, creatorUserId, std::string(Domain::ProjectRoleAdmin)},
                   "Insert project creator membership");

        exec(connection.get(), "COMMIT", "Commit create project transaction");

        auto result = execParams(connection.get(), std::string(ProjectSelectSql) + " WHERE p.id = $1",
                                 {projectId}, "Read created project");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Created project could not be read back");
        }
        return readProject(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback create project transaction");
        } catch (...) {
        }
        throw;
    }
}

bool PostgresDatabase::setProjectArchived(const std::string& projectKey, const bool archived) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE projects SET archived = $1::boolean, archived_at = CASE WHEN $1::boolean THEN CURRENT_TIMESTAMP ELSE NULL END, updated_at = CURRENT_TIMESTAMP
WHERE project_key = $2 AND deleted_at IS NULL
)SQL",
                             {archived ? std::string("true") : std::string("false"), projectKey},
                             "Set project archived");
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool PostgresDatabase::softDeleteProject(const std::string& projectKey, const std::string& actorUserId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE projects SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = $1, updated_at = CURRENT_TIMESTAMP
WHERE project_key = $2 AND deleted_at IS NULL
)SQL",
                             {actorUserId, projectKey}, "Soft delete project");
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool PostgresDatabase::restoreProject(const std::string& projectKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE projects SET deleted_at = NULL, deleted_by_user_id = NULL, updated_at = CURRENT_TIMESTAMP
WHERE project_key = $1 AND deleted_at IS NOT NULL
)SQL",
                             {projectKey}, "Restore project");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::Project> PostgresDatabase::listDeletedProjects() {
    auto connection = connect(connectionString_);
    // Fixed 90-day retention, checked on demand -- there is no background
    // job to purge proactively (D89/D51).
    exec(connection.get(),
        "DELETE FROM projects WHERE deleted_at IS NOT NULL AND deleted_at <= CURRENT_TIMESTAMP - INTERVAL '90 days'",
        "Purge expired recycle-bin projects");

    auto result = exec(connection.get(),
                       std::string(ProjectSelectSql) + " WHERE p.deleted_at IS NOT NULL ORDER BY p.deleted_at DESC",
                       "List deleted projects");
    std::vector<Domain::Project> projects;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        projects.push_back(readProject(result.get(), row));
    }
    return projects;
}

bool PostgresDatabase::permanentlyDeleteProject(const std::string& projectKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "DELETE FROM projects WHERE project_key = $1 AND deleted_at IS NOT NULL",
                             {projectKey}, "Permanently delete project");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::optional<std::string> PostgresDatabase::getSetting(const std::string& key) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "SELECT value FROM installation_settings WHERE setting_key = $1",
                             {key}, "Get installation setting");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return value(result.get(), 0, 0);
}

void PostgresDatabase::setSetting(const std::string& key, const std::string& value) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), R"SQL(
INSERT INTO installation_settings(setting_key, value, updated_at) VALUES ($1, $2, CURRENT_TIMESTAMP)
ON CONFLICT (setting_key) DO UPDATE SET value = EXCLUDED.value, updated_at = CURRENT_TIMESTAMP
)SQL",
               {key, value}, "Set installation setting");
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
    // `label` is checked via EXISTS rather than the already-aggregated `labels`
    // LATERAL join (which feeds the label-list summary column) -- filtering on
    // the joined row directly would restrict that aggregate to only the
    // matching label instead of the issue's full label list.
    const std::string sql = std::string(IssueSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND ($1::text IS NULL OR p.project_key = $1)
  AND ($2::text IS NULL OR s.status_key = $2)
  AND ($3::text IS NULL OR it.type_key = $3)
  AND ($4::text IS NULL OR pr.priority_key = $4)
  AND ($5::text IS NULL OR assignee.email = $5)
  AND ($6::text IS NULL OR i.due_date <= $6::date)
  AND ($7::text IS NULL OR EXISTS (
        SELECT 1 FROM issue_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.issue_id = i.id AND l2.name ILIKE $7))
  AND ($8::text IS NULL OR i.summary ILIKE $8 OR i.description ILIKE $8 OR i.issue_key ILIKE $8)
ORDER BY i.updated_at DESC, i.issue_key DESC
LIMIT 200
)SQL";
    auto result = execParams(connection.get(),
                             sql,
                             {filter.projectKey,
                              filter.statusKey,
                              filter.issueTypeKey,
                              filter.priorityKey,
                              filter.assigneeEmail,
                              filter.dueBefore,
                              filter.label,
                              filter.search ? std::optional<std::string>("%" + *filter.search + "%") : std::nullopt},
                             "List issues");
    std::vector<Domain::Issue> issues;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        issues.push_back(readIssue(result.get(), row));
    }
    return issues;
}

std::vector<Domain::Issue> PostgresDatabase::listIssues(const Domain::IssueFilter& filter, int limit, int offset) {
    auto connection = connect(connectionString_);
    const std::string sql = std::string(IssueSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND ($1::text IS NULL OR p.project_key = $1)
  AND ($2::text IS NULL OR s.status_key = $2)
  AND ($3::text IS NULL OR it.type_key = $3)
  AND ($4::text IS NULL OR pr.priority_key = $4)
  AND ($5::text IS NULL OR assignee.email = $5)
  AND ($6::text IS NULL OR i.due_date <= $6::date)
  AND ($7::text IS NULL OR EXISTS (
        SELECT 1 FROM issue_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.issue_id = i.id AND l2.name ILIKE $7))
  AND ($8::text IS NULL OR i.summary ILIKE $8 OR i.description ILIKE $8 OR i.issue_key ILIKE $8)
ORDER BY i.updated_at DESC, i.issue_key DESC
LIMIT $9::int OFFSET $10::int
)SQL";
    auto result = execParams(connection.get(),
                             sql,
                             {filter.projectKey,
                              filter.statusKey,
                              filter.issueTypeKey,
                              filter.priorityKey,
                              filter.assigneeEmail,
                              filter.dueBefore,
                              filter.label,
                              filter.search ? std::optional<std::string>("%" + *filter.search + "%") : std::nullopt,
                              std::optional<std::string>(std::to_string(limit)),
                              std::optional<std::string>(std::to_string(offset))},
                             "List issues (paginated)");
    std::vector<Domain::Issue> issues;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        issues.push_back(readIssue(result.get(), row));
    }
    return issues;
}

std::int64_t PostgresDatabase::countIssues(const Domain::IssueFilter& filter) {
    auto connection = connect(connectionString_);
    const std::string sql = R"SQL(
SELECT COUNT(*)
FROM issues i
JOIN projects p ON p.id = i.project_id
JOIN issue_types it ON it.id = i.issue_type_id
JOIN issue_statuses s ON s.id = i.status_id
JOIN priorities pr ON pr.id = i.priority_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND ($1::text IS NULL OR p.project_key = $1)
  AND ($2::text IS NULL OR s.status_key = $2)
  AND ($3::text IS NULL OR it.type_key = $3)
  AND ($4::text IS NULL OR pr.priority_key = $4)
  AND ($5::text IS NULL OR assignee.email = $5)
  AND ($6::text IS NULL OR i.due_date <= $6::date)
  AND ($7::text IS NULL OR EXISTS (
        SELECT 1 FROM issue_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.issue_id = i.id AND l2.name ILIKE $7))
  AND ($8::text IS NULL OR i.summary ILIKE $8 OR i.description ILIKE $8 OR i.issue_key ILIKE $8)
)SQL";
    auto result = execParams(connection.get(),
                             sql,
                             {filter.projectKey,
                              filter.statusKey,
                              filter.issueTypeKey,
                              filter.priorityKey,
                              filter.assigneeEmail,
                              filter.dueBefore,
                              filter.label,
                              filter.search ? std::optional<std::string>("%" + *filter.search + "%") : std::nullopt},
                             "Count issues");
    if (PQntuples(result.get()) == 0) {
        return 0;
    }
    return int64Value(result.get(), 0, 0);
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
        std::optional<std::string> parentId;
        if (request.parentIssueKey && !request.parentIssueKey->empty()) {
            parentId = lookupIssueId(connection.get(), *request.parentIssueKey);
        }

        // Simple integer manual order (D31): new issues are appended after
        // the highest existing rank within their project. The project row is
        // already FOR-UPDATE-locked above, which serializes this alongside
        // concurrent creates in the same project.
        auto maxRank = execParams(connection.get(),
                                  "SELECT COALESCE(MAX(rank_order), 0) + 1 FROM issues WHERE project_id = $1 AND deleted_at IS NULL",
                                  {projectId},
                                  "Compute next rank order");
        const std::int64_t rankOrder = int64Value(maxRank.get(), 0, 0);

        execParams(connection.get(), R"SQL(
INSERT INTO issues(id, project_id, issue_number, issue_key, summary, description,
                   issue_type_id, status_id, priority_id, reporter_user_id, assignee_user_id,
                   parent_issue_id, story_points, due_date, rank_order, created_at, updated_at)
VALUES ($1, $2, $3::bigint, $4, $5, $6, $7, $8, $9, $10, $11,
        $12, $13::double precision, $14::date, $15::bigint, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
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
                    parentId,
                    request.storyPoints ? std::optional<std::string>(std::to_string(*request.storyPoints)) : std::nullopt,
                    request.dueDate,
                    std::to_string(rankOrder)},
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
                                         const std::optional<std::string> resolution,
                                         const std::optional<std::int64_t> expectedVersion) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin status transaction");
    try {
        auto current = execParams(connection.get(), R"SQL(
SELECT i.id, s.status_key, s.category, i.version
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
        const std::string oldCategory = value(current.get(), 0, 2);
        const std::int64_t currentVersion = int64Value(current.get(), 0, 3);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Issue was modified by another user");
        }
        if (oldStatus == statusKey) {
            exec(connection.get(), "COMMIT", "Commit unchanged status transaction");
            return true;
        }

        auto targetStatus = execParams(connection.get(),
                                       "SELECT id, category FROM issue_statuses WHERE status_key = $1",
                                       {statusKey},
                                       "Lookup target status");
        if (PQntuples(targetStatus.get()) != 1) {
            throw std::invalid_argument("Unknown issue_statuses key: " + statusKey);
        }
        const std::string statusId = value(targetStatus.get(), 0, 0);
        const std::string targetCategory = value(targetStatus.get(), 0, 1);
        const std::string actorId = requireUserId(connection.get(), actorUserId);

        // The fixed workflow rules (D68-D70): completing an issue requires a
        // resolution and is blocked while any sub-task is unfinished;
        // reopening (leaving Done) always clears resolution and never
        // cascades to sub-tasks; any other transition leaves resolution
        // alone.
        bool touchResolution = false;
        std::optional<std::string> resolutionValue;
        if (targetCategory == "done") {
            if (!resolution || resolution->empty()) {
                throw Domain::WorkflowViolation("resolution is required when transitioning to a Done-category status");
            }
            if (!Domain::isValidResolution(*resolution)) {
                throw Domain::WorkflowViolation("Unknown resolution: " + *resolution);
            }
            auto unfinishedChild = execParams(connection.get(), R"SQL(
SELECT 1
FROM issues child
JOIN issue_statuses cs ON cs.id = child.status_id
WHERE child.parent_issue_id = $1 AND child.deleted_at IS NULL AND cs.category <> 'done'
LIMIT 1
)SQL",
                                              {issueId},
                                              "Check unfinished sub-tasks");
            if (PQntuples(unfinishedChild.get()) != 0) {
                throw Domain::WorkflowViolation("Cannot complete an issue while it has unfinished sub-tasks");
            }
            touchResolution = true;
            resolutionValue = resolution;
        } else if (oldCategory == "done") {
            touchResolution = true;
            resolutionValue = std::nullopt;
        }

        if (touchResolution) {
            execParams(connection.get(),
                       "UPDATE issues SET status_id = $1, resolution = $2, version = version + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $3",
                       {statusId, resolutionValue, issueId},
                       "Update issue status");
        } else {
            execParams(connection.get(),
                       "UPDATE issues SET status_id = $1, version = version + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $2",
                       {statusId, issueId},
                       "Update issue status");
        }
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

namespace {
std::string historyText(const std::optional<std::string>& value) {
    return value.value_or(std::string());
}
std::string historyText(const std::optional<double>& value) {
    return value ? std::to_string(*value) : std::string();
}
} // namespace

std::optional<Domain::Issue> PostgresDatabase::editIssue(const std::string& issueKey,
                                                         const Domain::EditIssueRequest& request,
                                                         const std::string& actorUserId,
                                                         const std::optional<std::int64_t> expectedVersion) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin edit issue transaction");
    try {
        auto current = execParams(connection.get(), R"SQL(
SELECT i.id, i.summary, i.description, pr.priority_key, assignee.email,
       i.story_points, i.due_date::text, i.version
FROM issues i
JOIN priorities pr ON pr.id = i.priority_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
WHERE i.deleted_at IS NULL
  AND (i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1))
FOR UPDATE OF i
)SQL",
                                  {issueKey},
                                  "Lock issue for edit");
        if (PQntuples(current.get()) == 0) {
            exec(connection.get(), "ROLLBACK", "Rollback missing issue transaction");
            return std::nullopt;
        }
        const std::string issueId = value(current.get(), 0, 0);
        const std::string oldSummary = value(current.get(), 0, 1);
        const std::string oldDescription = value(current.get(), 0, 2);
        const std::string oldPriorityKey = value(current.get(), 0, 3);
        const std::optional<std::string> oldAssigneeEmail = optionalValue(current.get(), 0, 4);
        std::optional<double> oldStoryPoints;
        if (PQgetisnull(current.get(), 0, 5) == 0) {
            oldStoryPoints = std::stod(value(current.get(), 0, 5));
        }
        const std::optional<std::string> oldDueDate = optionalValue(current.get(), 0, 6);
        const std::int64_t currentVersion = int64Value(current.get(), 0, 7);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Issue was modified by another user");
        }

        const std::string priorityId = lookupId(connection.get(), "priorities", "priority_key", request.priorityKey);
        std::optional<std::string> assigneeId;
        if (request.assigneeEmail && !request.assigneeEmail->empty()) {
            assigneeId = lookupId(connection.get(), "users", "email", *request.assigneeEmail);
        }
        const std::string actorId = requireUserId(connection.get(), actorUserId);

        execParams(connection.get(), R"SQL(
UPDATE issues
SET summary = $1, description = $2, priority_id = $3, assignee_user_id = $4,
    story_points = $5::double precision, due_date = $6::date, version = version + 1, updated_at = CURRENT_TIMESTAMP
WHERE id = $7
)SQL",
                   {request.summary,
                    request.description,
                    priorityId,
                    assigneeId,
                    request.storyPoints ? std::optional<std::string>(std::to_string(*request.storyPoints)) : std::nullopt,
                    request.dueDate,
                    issueId},
                   "Update issue fields");

        execParams(connection.get(), "DELETE FROM issue_labels WHERE issue_id = $1", {issueId}, "Clear issue labels");
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

        auto recordHistory = [&](const char* field, const std::string& oldValue, const std::string& newValue) {
            if (oldValue == newValue) {
                return;
            }
            std::optional<std::string> oldParam = oldValue.empty() ? std::nullopt : std::optional<std::string>(oldValue);
            std::optional<std::string> newParam = newValue.empty() ? std::nullopt : std::optional<std::string>(newValue);
            execParams(connection.get(), R"SQL(
INSERT INTO issue_history(id, issue_id, actor_user_id, field_name, old_value, new_value)
VALUES ($1, $2, $3, $4, $5, $6)
)SQL",
                       {Common::uuidV4(), issueId, actorId, std::string(field), oldParam, newParam},
                       "Insert edit history");
        };
        recordHistory("summary", oldSummary, request.summary);
        recordHistory("description", oldDescription, request.description);
        recordHistory("priority", oldPriorityKey, request.priorityKey);
        recordHistory("assignee", historyText(oldAssigneeEmail), historyText(request.assigneeEmail));
        recordHistory("story_points", historyText(oldStoryPoints), historyText(request.storyPoints));
        recordHistory("due_date", historyText(oldDueDate), historyText(request.dueDate));

        exec(connection.get(), "COMMIT", "Commit edit issue transaction");
        auto result = execParams(connection.get(), std::string(IssueSelect) + " WHERE i.deleted_at IS NULL AND i.id = $1",
                                 {issueId}, "Read edited issue");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Edited issue could not be read back");
        }
        return readIssue(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback edit issue transaction");
        } catch (...) {
        }
        throw;
    }
}

Domain::Issue PostgresDatabase::reorderIssue(const std::string& issueKey,
                                             std::optional<std::string> beforeIssueKey) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin reorder issue transaction");
    try {
        const std::string issueId = lookupIssueId(connection.get(), issueKey);
        auto projectRow = execParams(connection.get(),
                                     "SELECT project_id FROM issues WHERE id = $1 FOR UPDATE",
                                     {issueId},
                                     "Lock issue for reorder");
        const std::string projectId = value(projectRow.get(), 0, 0);

        std::optional<std::string> beforeIssueId;
        if (beforeIssueKey.has_value() && !beforeIssueKey->empty()) {
            const std::string resolvedBeforeId = lookupIssueId(connection.get(), *beforeIssueKey);
            if (resolvedBeforeId == issueId) {
                throw std::invalid_argument("Cannot reorder an issue before itself");
            }
            auto beforeProjectRow = execParams(connection.get(),
                                               "SELECT project_id FROM issues WHERE id = $1",
                                               {resolvedBeforeId},
                                               "Read reorder anchor project");
            if (value(beforeProjectRow.get(), 0, 0) != projectId) {
                throw std::invalid_argument("Cannot reorder relative to an issue in a different project");
            }
            beforeIssueId = resolvedBeforeId;
        }

        // Full renumbering pass (D31): sufficient for small per-project issue
        // counts, and simpler than a minimal-diff fractional/shift scheme.
        auto listResult = execParams(connection.get(),
                                     "SELECT id FROM issues WHERE project_id = $1 AND deleted_at IS NULL ORDER BY rank_order, issue_number",
                                     {projectId},
                                     "List project issues for reorder");
        std::vector<std::string> orderedIds;
        orderedIds.reserve(static_cast<std::size_t>(PQntuples(listResult.get())));
        for (int row = 0; row < PQntuples(listResult.get()); ++row) {
            orderedIds.push_back(value(listResult.get(), row, 0));
        }

        orderedIds.erase(std::remove(orderedIds.begin(), orderedIds.end(), issueId), orderedIds.end());
        if (beforeIssueId.has_value()) {
            const auto position = std::find(orderedIds.begin(), orderedIds.end(), *beforeIssueId);
            orderedIds.insert(position, issueId);
        } else {
            orderedIds.push_back(issueId);
        }

        for (std::size_t index = 0; index < orderedIds.size(); ++index) {
            const std::int64_t newRank = static_cast<std::int64_t>(index) + 1;
            execParams(connection.get(),
                       "UPDATE issues SET rank_order = $1::bigint WHERE id = $2 AND rank_order <> $1::bigint",
                       {std::to_string(newRank), orderedIds[index]},
                       "Update issue rank order");
        }

        exec(connection.get(), "COMMIT", "Commit reorder issue transaction");
        auto result = execParams(connection.get(), std::string(IssueSelect) + " WHERE i.deleted_at IS NULL AND i.id = $1",
                                 {issueId}, "Read reordered issue");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Reordered issue could not be read back");
        }
        return readIssue(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback reorder issue transaction");
        } catch (...) {
        }
        throw;
    }
}

Domain::Issue PostgresDatabase::moveIssue(const std::string& issueKey,
                                          const std::string& targetProjectKey,
                                          const std::string& actorUserId) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin move issue transaction");
    try {
        const std::string issueId = lookupIssueId(connection.get(), issueKey);
        auto current = execParams(connection.get(), R"SQL(
SELECT i.project_id, p.project_key, i.issue_key, i.parent_issue_id
FROM issues i JOIN projects p ON p.id = i.project_id
WHERE i.id = $1
FOR UPDATE OF i
)SQL",
                                  {issueId},
                                  "Lock issue for move");
        const std::string currentProjectId = value(current.get(), 0, 0);
        const std::string currentProjectKey = value(current.get(), 0, 1);
        const std::string currentIssueKey = value(current.get(), 0, 2);
        const bool hasParent = PQgetisnull(current.get(), 0, 3) == 0;
        if (hasParent) {
            throw std::invalid_argument("Cannot move an issue that has a parent");
        }

        auto childCheck = execParams(connection.get(),
                                     "SELECT COUNT(*) FROM issues WHERE parent_issue_id = $1 AND deleted_at IS NULL",
                                     {issueId},
                                     "Count child issues");
        if (int64Value(childCheck.get(), 0, 0) > 0) {
            throw std::invalid_argument("Cannot move an issue that has child issues");
        }

        auto project = execParams(connection.get(),
                                  "SELECT id, next_issue_number FROM projects WHERE project_key = $1 AND archived = FALSE AND deleted_at IS NULL FOR UPDATE",
                                  {targetProjectKey},
                                  "Lock target project");
        if (PQntuples(project.get()) != 1) {
            throw std::invalid_argument("Unknown project: " + targetProjectKey);
        }
        const std::string targetProjectId = value(project.get(), 0, 0);
        if (targetProjectId == currentProjectId) {
            throw std::invalid_argument("Issue is already in project: " + targetProjectKey);
        }
        const std::int64_t issueNumber = int64Value(project.get(), 0, 1);
        const std::string newIssueKey = targetProjectKey + "-" + std::to_string(issueNumber);

        execParams(connection.get(),
                   "UPDATE projects SET next_issue_number = next_issue_number + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $1",
                   {targetProjectId},
                   "Increment target project issue counter");

        // Append-at-end within the target project, same as createIssue (D31).
        auto maxRank = execParams(connection.get(),
                                  "SELECT COALESCE(MAX(rank_order), 0) + 1 FROM issues WHERE project_id = $1 AND deleted_at IS NULL",
                                  {targetProjectId},
                                  "Compute next rank order for move");
        const std::int64_t rankOrder = int64Value(maxRank.get(), 0, 0);

        execParams(connection.get(), R"SQL(
UPDATE issues
SET project_id = $1, issue_number = $2::bigint, issue_key = $3, rank_order = $4::bigint, updated_at = CURRENT_TIMESTAMP
WHERE id = $5
)SQL",
                   {targetProjectId, std::to_string(issueNumber), newIssueKey, std::to_string(rankOrder), issueId},
                   "Update issue for move");

        // The vacated key becomes a permanent alias (D38); safe because
        // issue_key_aliases.alias_key is a PRIMARY KEY (no collision) and
        // issue numbers/keys are never reused.
        execParams(connection.get(),
                   "INSERT INTO issue_key_aliases(alias_key, issue_id) VALUES ($1, $2)",
                   {currentIssueKey, issueId},
                   "Insert issue key alias");

        const std::string actorId = requireUserId(connection.get(), actorUserId);
        execParams(connection.get(), R"SQL(
INSERT INTO issue_history(id, issue_id, actor_user_id, field_name, old_value, new_value)
VALUES ($1, $2, $3, 'project', $4, $5)
)SQL",
                   {Common::uuidV4(), issueId, actorId, currentProjectKey, targetProjectKey},
                   "Insert move history");

        exec(connection.get(), "COMMIT", "Commit move issue transaction");
        auto result = execParams(connection.get(), std::string(IssueSelect) + " WHERE i.deleted_at IS NULL AND i.id = $1",
                                 {issueId}, "Read moved issue");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Moved issue could not be read back");
        }
        return readIssue(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback move issue transaction");
        } catch (...) {
        }
        throw;
    }
}

std::vector<Domain::Comment> PostgresDatabase::listComments(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(CommentSelect) + R"SQL(
JOIN issues i ON i.id = c.issue_id
WHERE c.deleted_at IS NULL
  AND i.deleted_at IS NULL
  AND (i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1))
ORDER BY c.created_at
)SQL",
                             {issueKey},
                             "List comments");
    std::vector<Domain::Comment> comments;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        comments.push_back(readComment(result.get(), row));
    }
    return comments;
}

Domain::Comment PostgresDatabase::addComment(const Domain::AddCommentRequest& request,
                                             const std::string& authorUserId) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), request.issueKey);
    const std::string authorId = requireUserId(connection.get(), authorUserId);
    const std::string commentId = Common::uuidV4();
    execParams(connection.get(), R"SQL(
INSERT INTO comments(id, issue_id, author_user_id, body, created_at, updated_at)
VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
               {commentId, issueId, authorId, request.body},
               "Insert comment");
    auto result = execParams(connection.get(), std::string(CommentSelect) + "WHERE c.id = $1", {commentId}, "Read created comment");
    if (PQntuples(result.get()) != 1) {
        throw std::runtime_error("Created comment could not be read back");
    }
    return readComment(result.get(), 0);
}

std::optional<Domain::Comment> PostgresDatabase::findCommentById(const std::string& commentId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(CommentSelect) + "WHERE c.id = $1 AND c.deleted_at IS NULL", {commentId}, "Find comment");
    if (PQntuples(result.get()) != 1) {
        return std::nullopt;
    }
    return readComment(result.get(), 0);
}

// `actorUserId` is unused: D81's simplified edited-flag schema has no
// per-edit actor column (unlike issue_history) -- only `edited_at` is
// tracked. Kept in the signature for symmetry with editIssue and in case a
// future decision adds an `edited_by_user_id` column.
std::optional<Domain::Comment> PostgresDatabase::editComment(const std::string& commentId,
                                                              const std::string& body,
                                                              const std::string& /*actorUserId*/,
                                                              const std::optional<std::int64_t> expectedVersion) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin edit comment transaction");
    try {
        auto current = execParams(connection.get(),
                                  "SELECT version FROM comments WHERE id = $1 AND deleted_at IS NULL FOR UPDATE",
                                  {commentId},
                                  "Lock comment for edit");
        if (PQntuples(current.get()) == 0) {
            exec(connection.get(), "ROLLBACK", "Rollback missing comment transaction");
            return std::nullopt;
        }
        const std::int64_t currentVersion = int64Value(current.get(), 0, 0);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Comment was modified by another user");
        }

        execParams(connection.get(), R"SQL(
UPDATE comments SET body = $1, version = version + 1, edited_at = CURRENT_TIMESTAMP, updated_at = CURRENT_TIMESTAMP
WHERE id = $2
)SQL",
                   {body, commentId},
                   "Update comment");

        exec(connection.get(), "COMMIT", "Commit edit comment transaction");
        auto result = execParams(connection.get(), std::string(CommentSelect) + "WHERE c.id = $1", {commentId}, "Read edited comment");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Edited comment could not be read back");
        }
        return readComment(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback edit comment transaction");
        } catch (...) {
        }
        throw;
    }
}

bool PostgresDatabase::deleteComment(const std::string& commentId, const std::string& actorUserId) {
    auto connection = connect(connectionString_);
    const std::string actorId = requireUserId(connection.get(), actorUserId);
    auto result = execParams(connection.get(), R"SQL(
UPDATE comments SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = $1, updated_at = CURRENT_TIMESTAMP
WHERE id = $2 AND deleted_at IS NULL
)SQL",
                             {actorId, commentId},
                             "Delete comment");
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool PostgresDatabase::addCommentReaction(const std::string& commentId, const std::string& userId,
                                          const std::string& reactionKey) {
    auto connection = connect(connectionString_);
    const std::string resolvedUserId = requireUserId(connection.get(), userId);
    auto result = execParams(connection.get(), R"SQL(
INSERT INTO comment_reactions(comment_id, user_id, reaction_key) VALUES ($1, $2, $3)
ON CONFLICT (comment_id, user_id, reaction_key) DO NOTHING
)SQL",
                             {commentId, resolvedUserId, reactionKey},
                             "Add comment reaction");
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool PostgresDatabase::removeCommentReaction(const std::string& commentId, const std::string& userId,
                                             const std::string& reactionKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
DELETE FROM comment_reactions WHERE comment_id = $1 AND user_id = $2 AND reaction_key = $3
)SQL",
                             {commentId, userId, reactionKey},
                             "Remove comment reaction");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::CommentReaction> PostgresDatabase::listCommentReactions(const std::string& commentId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT r.reaction_key, u.id, u.display_name, u.email
FROM comment_reactions r JOIN users u ON u.id = r.user_id
WHERE r.comment_id = $1
ORDER BY r.reaction_key, u.display_name
)SQL",
                             {commentId},
                             "List comment reactions");
    std::vector<Domain::CommentReaction> reactions;
    const int rowCount = PQntuples(result.get());
    for (int row = 0; row < rowCount; ++row) {
        Domain::CommentReaction reaction;
        reaction.reactionKey = value(result.get(), row, 0);
        reaction.user = readUserSummary(result.get(), row, 1);
        reactions.push_back(std::move(reaction));
    }
    return reactions;
}

namespace {
Domain::Notification readNotification(PGresult* result, int row) {
    Domain::Notification notification;
    notification.id = value(result, row, 0);
    notification.type = value(result, row, 1);
    notification.issueKey = optionalValue(result, row, 2);
    notification.issueSummary = optionalValue(result, row, 3);
    notification.readAt = optionalValue(result, row, 4);
    notification.createdAt = value(result, row, 5);
    return notification;
}

constexpr const char* NotificationSelect = R"SQL(
SELECT n.id, n.type, i.issue_key, i.summary, n.read_at::text, n.created_at::text
FROM notifications n
LEFT JOIN issues i ON i.id = n.issue_id
)SQL";
} // namespace

Domain::Notification PostgresDatabase::createNotification(const std::string& userId,
                                                           const std::string& type,
                                                           const std::string& issueId) {
    auto connection = connect(connectionString_);
    const std::string notificationId = Common::uuidV4();
    execParams(connection.get(), R"SQL(
INSERT INTO notifications(id, user_id, type, issue_id, created_at) VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)
)SQL",
               {notificationId, userId, type, issueId},
               "Insert notification");

    auto result = execParams(connection.get(), std::string(NotificationSelect) + "WHERE n.id = $1",
                             {notificationId}, "Read created notification");
    if (PQntuples(result.get()) != 1) {
        throw std::runtime_error("Created notification could not be read back");
    }
    return readNotification(result.get(), 0);
}

std::vector<Domain::Notification> PostgresDatabase::listNotifications(const std::string& userId, const bool unreadOnly) {
    auto connection = connect(connectionString_);
    std::string sql = std::string(NotificationSelect) + "WHERE n.user_id = $1";
    if (unreadOnly) {
        sql += " AND n.read_at IS NULL";
    }
    sql += " ORDER BY n.created_at DESC";
    auto result = execParams(connection.get(), sql, {userId}, "List notifications");
    std::vector<Domain::Notification> notifications;
    const int rowCount = PQntuples(result.get());
    for (int row = 0; row < rowCount; ++row) {
        notifications.push_back(readNotification(result.get(), row));
    }
    return notifications;
}

int PostgresDatabase::countUnreadNotifications(const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "SELECT COUNT(*) FROM notifications WHERE user_id = $1 AND read_at IS NULL",
                             {userId}, "Count unread notifications");
    if (PQntuples(result.get()) == 0) {
        return 0;
    }
    return static_cast<int>(int64Value(result.get(), 0, 0));
}

bool PostgresDatabase::markNotificationRead(const std::string& notificationId, const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE notifications SET read_at = CURRENT_TIMESTAMP WHERE id = $1 AND user_id = $2 AND read_at IS NULL
)SQL",
                             {notificationId, userId},
                             "Mark notification read");
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool PostgresDatabase::markAllNotificationsRead(const std::string& userId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(),
                             "UPDATE notifications SET read_at = CURRENT_TIMESTAMP WHERE user_id = $1 AND read_at IS NULL",
                             {userId},
                             "Mark all notifications read");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::Worklog> PostgresDatabase::listWorklogs(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(WorklogSelect) + R"SQL(
JOIN issues i ON i.id = w.issue_id
WHERE w.deleted_at IS NULL
  AND i.deleted_at IS NULL
  AND (i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1))
ORDER BY w.work_date DESC, w.created_at DESC
)SQL",
                             {issueKey},
                             "List worklogs");
    std::vector<Domain::Worklog> worklogs;
    const int rowCount = PQntuples(result.get());
    for (int row = 0; row < rowCount; ++row) {
        worklogs.push_back(readWorklog(result.get(), row));
    }
    return worklogs;
}

Domain::Worklog PostgresDatabase::addWorklog(const Domain::AddWorklogRequest& request, const std::string& authorUserId) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), request.issueKey);
    const std::string authorId = requireUserId(connection.get(), authorUserId);
    const std::string worklogId = Common::uuidV4();

    execParams(connection.get(), R"SQL(
INSERT INTO worklogs(id, issue_id, author_user_id, work_date, time_spent_seconds, comment, created_at, updated_at)
VALUES ($1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
               {worklogId, issueId, authorId, request.workDate, std::to_string(request.timeSpentSeconds), request.comment},
               "Insert worklog");

    auto result = execParams(connection.get(), std::string(WorklogSelect) + "WHERE w.id = $1", {worklogId}, "Read created worklog");
    if (PQntuples(result.get()) != 1) {
        throw std::runtime_error("Created worklog could not be read back");
    }
    return readWorklog(result.get(), 0);
}

std::optional<Domain::Worklog> PostgresDatabase::findWorklogById(const std::string& worklogId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(WorklogSelect) + "WHERE w.id = $1 AND w.deleted_at IS NULL",
                             {worklogId}, "Find worklog");
    if (PQntuples(result.get()) != 1) {
        return std::nullopt;
    }
    return readWorklog(result.get(), 0);
}

std::optional<Domain::Worklog> PostgresDatabase::editWorklog(const std::string& worklogId,
                                                              const Domain::EditWorklogRequest& request,
                                                              const std::optional<std::int64_t> expectedVersion) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin edit worklog transaction");
    try {
        auto current = execParams(connection.get(),
                                  "SELECT version FROM worklogs WHERE id = $1 AND deleted_at IS NULL FOR UPDATE",
                                  {worklogId},
                                  "Lock worklog for edit");
        if (PQntuples(current.get()) == 0) {
            exec(connection.get(), "ROLLBACK", "Rollback missing worklog transaction");
            return std::nullopt;
        }
        const std::int64_t currentVersion = int64Value(current.get(), 0, 0);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Worklog was modified by another user");
        }

        execParams(connection.get(), R"SQL(
UPDATE worklogs SET work_date = $1, time_spent_seconds = $2, comment = $3, version = version + 1, updated_at = CURRENT_TIMESTAMP
WHERE id = $4
)SQL",
                   {request.workDate, std::to_string(request.timeSpentSeconds), request.comment, worklogId},
                   "Update worklog");

        exec(connection.get(), "COMMIT", "Commit edit worklog transaction");
        auto result = execParams(connection.get(), std::string(WorklogSelect) + "WHERE w.id = $1", {worklogId}, "Read edited worklog");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Edited worklog could not be read back");
        }
        return readWorklog(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback edit worklog transaction");
        } catch (...) {
        }
        throw;
    }
}

bool PostgresDatabase::deleteWorklog(const std::string& worklogId, const std::string& actorUserId) {
    auto connection = connect(connectionString_);
    const std::string actorId = requireUserId(connection.get(), actorUserId);
    auto result = execParams(connection.get(), R"SQL(
UPDATE worklogs SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = $1, updated_at = CURRENT_TIMESTAMP
WHERE id = $2 AND deleted_at IS NULL
)SQL",
                             {actorId, worklogId},
                             "Delete worklog");
    return std::string(PQcmdTuples(result.get())) != "0";
}

void PostgresDatabase::recordAuditEvent(const std::string& category,
                                        const std::string& action,
                                        const std::optional<std::string> actorUserId,
                                        const std::optional<std::string> targetType,
                                        const std::optional<std::string> targetId,
                                        const std::optional<std::string> details) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), R"SQL(
INSERT INTO audit_events(id, category, action, actor_user_id, target_type, target_id, details, created_at)
VALUES ($1, $2, $3, $4, $5, $6, $7, CURRENT_TIMESTAMP)
)SQL",
               {Common::uuidV4(), category, action, actorUserId, targetType, targetId, details},
               "Insert audit event");
}

std::vector<Domain::AuditEvent> PostgresDatabase::listAuditEvents(const int limit) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(AuditEventSelect) + "ORDER BY a.created_at DESC LIMIT $1",
                             {std::to_string(limit)}, "List audit events");
    std::vector<Domain::AuditEvent> events;
    const int rowCount = PQntuples(result.get());
    for (int row = 0; row < rowCount; ++row) {
        events.push_back(readAuditEvent(result.get(), row));
    }
    return events;
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

std::vector<Domain::BoardColumn> PostgresDatabase::listBoardColumns() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), BoardColumnSelect, "List board columns");
    std::vector<Domain::BoardColumn> columns;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        columns.push_back(readBoardColumn(result.get(), row));
    }
    return columns;
}

bool PostgresDatabase::setBoardColumnWipLimit(const std::string& statusKey, const std::optional<int> wipLimit) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE board_columns SET wip_limit = $1
WHERE status_id = (SELECT id FROM issue_statuses WHERE status_key = $2)
)SQL",
                             {wipLimit ? std::optional<std::string>(std::to_string(*wipLimit)) : std::nullopt, statusKey},
                             "Set board column WIP limit");
    return std::string(PQcmdTuples(result.get())) != "0";
}

Domain::IssueLink PostgresDatabase::createIssueLink(const std::string& sourceIssueKey,
                                                    const std::string& targetIssueKey,
                                                    const std::string& linkType) {
    auto connection = connect(connectionString_);
    const std::string sourceId = lookupIssueId(connection.get(), sourceIssueKey);
    const std::string targetId = lookupIssueId(connection.get(), targetIssueKey);

    auto duplicate = execParams(connection.get(),
        "SELECT 1 FROM issue_links WHERE source_issue_id = $1 AND target_issue_id = $2 AND link_type = $3",
        {sourceId, targetId, linkType},
        "Check duplicate issue link");
    if (PQntuples(duplicate.get()) != 0) {
        throw std::invalid_argument("That link already exists");
    }

    const std::string linkId = Common::uuidV4();
    execParams(connection.get(),
               "INSERT INTO issue_links(id, source_issue_id, target_issue_id, link_type) VALUES ($1, $2, $3, $4)",
               {linkId, sourceId, targetId, linkType},
               "Insert issue link");

    Domain::IssueLink link;
    link.id = linkId;
    link.linkType = linkType;
    link.outward = true;

    auto targetRow = execParams(connection.get(), "SELECT issue_key, summary FROM issues WHERE id = $1",
                                {targetId}, "Read linked issue");
    if (PQntuples(targetRow.get()) == 1) {
        link.otherIssueKey = value(targetRow.get(), 0, 0);
        link.otherIssueSummary = value(targetRow.get(), 0, 1);
    }
    return link;
}

std::vector<Domain::IssueLink> PostgresDatabase::listIssueLinks(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);

    auto result = execParams(connection.get(), R"SQL(
SELECT l.id, l.link_type, TRUE AS outward, tgt.issue_key, tgt.summary
FROM issue_links l JOIN issues tgt ON tgt.id = l.target_issue_id
WHERE l.source_issue_id = $1 AND tgt.deleted_at IS NULL
UNION ALL
SELECT l.id, l.link_type, FALSE AS outward, src.issue_key, src.summary
FROM issue_links l JOIN issues src ON src.id = l.source_issue_id
WHERE l.target_issue_id = $1 AND src.deleted_at IS NULL
)SQL",
                             {issueId},
                             "List issue links");
    std::vector<Domain::IssueLink> links;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        Domain::IssueLink link;
        link.id = value(result.get(), row, 0);
        link.linkType = value(result.get(), row, 1);
        link.outward = boolValue(result.get(), row, 2);
        link.otherIssueKey = value(result.get(), row, 3);
        link.otherIssueSummary = value(result.get(), row, 4);
        links.push_back(std::move(link));
    }
    return links;
}

std::optional<Domain::IssueLinkDetail> PostgresDatabase::findIssueLinkById(const std::string& linkId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT l.id, l.link_type, src.issue_key, srcProj.project_key, tgt.issue_key, tgtProj.project_key
FROM issue_links l
JOIN issues src ON src.id = l.source_issue_id
JOIN projects srcProj ON srcProj.id = src.project_id
JOIN issues tgt ON tgt.id = l.target_issue_id
JOIN projects tgtProj ON tgtProj.id = tgt.project_id
WHERE l.id = $1
)SQL",
                             {linkId},
                             "Find issue link");
    if (PQntuples(result.get()) != 1) {
        return std::nullopt;
    }
    Domain::IssueLinkDetail detail;
    detail.id = value(result.get(), 0, 0);
    detail.linkType = value(result.get(), 0, 1);
    detail.sourceIssueKey = value(result.get(), 0, 2);
    detail.sourceProjectKey = value(result.get(), 0, 3);
    detail.targetIssueKey = value(result.get(), 0, 4);
    detail.targetProjectKey = value(result.get(), 0, 5);
    return detail;
}

bool PostgresDatabase::deleteIssueLink(const std::string& linkId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "DELETE FROM issue_links WHERE id = $1", {linkId}, "Delete issue link");
    return std::string(PQcmdTuples(result.get())) != "0";
}

namespace {
// Shared by the two structurally-identical watch/vote tables (issue_watchers,
// issue_votes: both (issue_id, user_id) composite-PK many-to-many tables with
// no other columns worth reading back). `table` is always a fixed internal
// literal, never caller input, matching the existing `lookupId` pattern.
bool insertMembership(PGconn* connection, const char* table, const std::string& issueId, const std::string& userId) {
    auto result = execParams(connection,
                             std::string("INSERT INTO ") + table + "(issue_id, user_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
                             {issueId, userId},
                             std::string("Insert into ") + table);
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool deleteMembership(PGconn* connection, const char* table, const std::string& issueId, const std::string& userId) {
    auto result = execParams(connection,
                             std::string("DELETE FROM ") + table + " WHERE issue_id = $1 AND user_id = $2",
                             {issueId, userId},
                             std::string("Delete from ") + table);
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::UserSummary> listMembers(PGconn* connection, const char* table, const std::string& issueId) {
    auto result = execParams(connection,
                             std::string("SELECT u.id, u.display_name, u.email FROM ") + table
                                 + " t JOIN users u ON u.id = t.user_id WHERE t.issue_id = $1 ORDER BY u.display_name",
                             {issueId},
                             std::string("List ") + table);
    std::vector<Domain::UserSummary> users;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        users.push_back(readUserSummary(result.get(), row, 0));
    }
    return users;
}
} // namespace

bool PostgresDatabase::watchIssue(const std::string& issueKey, const std::string& userId) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);
    const std::string resolvedUserId = requireUserId(connection.get(), userId);
    return insertMembership(connection.get(), "issue_watchers", issueId, resolvedUserId);
}

bool PostgresDatabase::unwatchIssue(const std::string& issueKey, const std::string& userId) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);
    return deleteMembership(connection.get(), "issue_watchers", issueId, userId);
}

std::vector<Domain::UserSummary> PostgresDatabase::listWatchers(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);
    return listMembers(connection.get(), "issue_watchers", issueId);
}

std::vector<Domain::Issue> PostgresDatabase::listWatchedIssues(const std::string& userId, int limit) {
    auto connection = connect(connectionString_);
    const std::string sql = std::string(IssueSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND EXISTS (SELECT 1 FROM issue_watchers w WHERE w.issue_id = i.id AND w.user_id = $1)
ORDER BY i.updated_at DESC
LIMIT $2
)SQL";
    auto result = execParams(connection.get(), sql, {userId, std::to_string(limit)}, "List watched issues");
    std::vector<Domain::Issue> issues;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        issues.push_back(readIssue(result.get(), row));
    }
    return issues;
}

bool PostgresDatabase::voteIssue(const std::string& issueKey, const std::string& userId) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);
    const std::string resolvedUserId = requireUserId(connection.get(), userId);
    return insertMembership(connection.get(), "issue_votes", issueId, resolvedUserId);
}

bool PostgresDatabase::unvoteIssue(const std::string& issueKey, const std::string& userId) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);
    return deleteMembership(connection.get(), "issue_votes", issueId, userId);
}

std::vector<Domain::UserSummary> PostgresDatabase::listVoters(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);
    return listMembers(connection.get(), "issue_votes", issueId);
}

bool PostgresDatabase::softDeleteIssue(const std::string& issueKey, const std::string& actorUserId) {
    auto connection = connect(connectionString_);
    const std::string actorId = requireUserId(connection.get(), actorUserId);
    auto result = execParams(connection.get(), R"SQL(
UPDATE issues SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = $1, updated_at = CURRENT_TIMESTAMP
WHERE (issue_key = $2 OR id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $2)) AND deleted_at IS NULL
)SQL",
                             {actorId, issueKey},
                             "Soft delete issue");
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool PostgresDatabase::restoreIssue(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE issues SET deleted_at = NULL, deleted_by_user_id = NULL, updated_at = CURRENT_TIMESTAMP
WHERE issue_key = $1 AND deleted_at IS NOT NULL
)SQL",
                             {issueKey},
                             "Restore issue");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::Issue> PostgresDatabase::listDeletedIssues() {
    auto connection = connect(connectionString_);
    exec(connection.get(), "DELETE FROM issues WHERE deleted_at IS NOT NULL AND deleted_at <= CURRENT_TIMESTAMP - INTERVAL '90 days'",
        "Purge expired deleted issues");

    const std::string sql = std::string(IssueSelect) + R"SQL(
WHERE i.deleted_at IS NOT NULL
ORDER BY i.deleted_at DESC
)SQL";
    auto result = exec(connection.get(), sql, "List deleted issues");
    std::vector<Domain::Issue> issues;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        issues.push_back(readIssue(result.get(), row));
    }
    return issues;
}

bool PostgresDatabase::permanentlyDeleteIssue(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "DELETE FROM issues WHERE issue_key = $1 AND deleted_at IS NOT NULL",
                             {issueKey}, "Permanently delete issue");
    return std::string(PQcmdTuples(result.get())) != "0";
}

Domain::Attachment PostgresDatabase::createAttachment(const std::string& id,
                                                      const std::string& issueKey,
                                                      const std::string& uploaderUserId,
                                                      const std::string& fileName,
                                                      const std::string& contentType,
                                                      const std::int64_t byteSize,
                                                      const std::string& sha256) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);
    const std::string uploaderId = requireUserId(connection.get(), uploaderUserId);
    execParams(connection.get(), R"SQL(
INSERT INTO attachments(id, issue_id, uploader_user_id, file_name, content_type, byte_size, storage_key, sha256)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
)SQL",
              {id, issueId, uploaderId, fileName, contentType, std::to_string(byteSize), id, sha256},
              "Create attachment");

    auto result = execParams(connection.get(), std::string(AttachmentSelect) + "WHERE a.id = $1", {id}, "Read created attachment");
    if (PQntuples(result.get()) == 0) {
        throw std::runtime_error("Failed to read back created attachment");
    }
    return readAttachment(result.get(), 0);
}

std::vector<Domain::Attachment> PostgresDatabase::listAttachments(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    const std::string issueId = lookupIssueId(connection.get(), issueKey);
    auto result = execParams(connection.get(),
                             std::string(AttachmentSelect) + "WHERE a.issue_id = $1 AND a.deleted_at IS NULL ORDER BY a.created_at",
                             {issueId}, "List attachments");
    std::vector<Domain::Attachment> attachments;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        attachments.push_back(readAttachment(result.get(), row));
    }
    return attachments;
}

std::optional<Domain::Attachment> PostgresDatabase::findAttachmentById(const std::string& attachmentId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(AttachmentSelect) + "WHERE a.id = $1", {attachmentId}, "Find attachment");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return readAttachment(result.get(), 0);
}

bool PostgresDatabase::softDeleteAttachment(const std::string& attachmentId, const std::string& actorUserId) {
    auto connection = connect(connectionString_);
    const std::string actorId = requireUserId(connection.get(), actorUserId);
    auto result = execParams(connection.get(), R"SQL(
UPDATE attachments SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = $1
WHERE id = $2 AND deleted_at IS NULL
)SQL",
                             {actorId, attachmentId}, "Soft delete attachment");
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool PostgresDatabase::restoreAttachment(const std::string& attachmentId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE attachments SET deleted_at = NULL, deleted_by_user_id = NULL
WHERE id = $1 AND deleted_at IS NOT NULL
)SQL",
                             {attachmentId}, "Restore attachment");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::Attachment> PostgresDatabase::listDeletedAttachments() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), std::string(AttachmentSelect) + "WHERE a.deleted_at IS NOT NULL ORDER BY a.deleted_at DESC",
                       "List deleted attachments");
    std::vector<Domain::Attachment> attachments;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        attachments.push_back(readAttachment(result.get(), row));
    }
    return attachments;
}

bool PostgresDatabase::permanentlyDeleteAttachment(const std::string& attachmentId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "DELETE FROM attachments WHERE id = $1 AND deleted_at IS NOT NULL",
                             {attachmentId}, "Permanently delete attachment");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<std::string> PostgresDatabase::listAttachmentStorageKeysForIssue(const std::string& issueKey) {
    auto connection = connect(connectionString_);
    // Deliberately does not use lookupIssueId (which excludes soft-deleted
    // issues): this is called right before a permanent delete, at which
    // point the issue is expected to already be soft-deleted.
    auto result = execParams(connection.get(), R"SQL(
SELECT a.storage_key FROM attachments a JOIN issues i ON i.id = a.issue_id
WHERE i.issue_key = $1 OR i.id = (SELECT issue_id FROM issue_key_aliases WHERE alias_key = $1)
)SQL",
                             {issueKey}, "List attachment storage keys for issue");
    std::vector<std::string> keys;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        keys.push_back(value(result.get(), row, 0));
    }
    return keys;
}

std::vector<std::string> PostgresDatabase::listAttachmentStorageKeysForProject(const std::string& projectKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT a.storage_key FROM attachments a
JOIN issues i ON i.id = a.issue_id
JOIN projects p ON p.id = i.project_id
WHERE p.project_key = $1
)SQL",
                             {projectKey}, "List attachment storage keys for project");
    std::vector<std::string> keys;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        keys.push_back(value(result.get(), row, 0));
    }
    return keys;
}

} // namespace TicketHub::Infrastructure::Database
