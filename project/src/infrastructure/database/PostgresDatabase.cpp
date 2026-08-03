#include "infrastructure/database/PostgresDatabase.h"

#include "common/FileUtil.h"
#include "common/RandomToken.h"
#include "common/Uuid.h"
#include "infrastructure/database/Migration.h"
#include "domain/Errors.h"

#include <libpq-fe.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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

Domain::Ticket readTicket(PGresult* result, int row) {
    Domain::Ticket ticket;
    ticket.id = value(result, row, 0);
    ticket.key = value(result, row, 1);
    ticket.number = int64Value(result, row, 2);
    ticket.projectKey = value(result, row, 3);
    ticket.projectName = value(result, row, 4);
    ticket.summary = value(result, row, 5);
    ticket.description = value(result, row, 6);
    ticket.type = {value(result, row, 7), value(result, row, 8), value(result, row, 9), value(result, row, 10)};
    ticket.status = {value(result, row, 11), value(result, row, 12), value(result, row, 13), intValue(result, row, 14)};
    ticket.priority = {value(result, row, 15), value(result, row, 16), intValue(result, row, 17), value(result, row, 18)};
    ticket.reporter = readUserSummary(result, row, 19);
    if (PQgetisnull(result, row, 22) == 0) {
        ticket.assignee = readUserSummary(result, row, 22);
    }
    ticket.parentTicketKey = optionalValue(result, row, 25);
    if (PQgetisnull(result, row, 26) == 0) {
        ticket.storyPoints = std::stod(value(result, row, 26));
    }
    ticket.dueDate = optionalValue(result, row, 27);
    ticket.labels = splitLabels(value(result, row, 28));
    ticket.createdAt = value(result, row, 29);
    ticket.updatedAt = value(result, row, 30);
    ticket.version = int64Value(result, row, 31);
    ticket.resolution = optionalValue(result, row, 32);
    ticket.rankOrder = int64Value(result, row, 33);
    if (PQgetisnull(result, row, 34) == 0) {
        ticket.component = Domain::ComponentSummary{value(result, row, 34), value(result, row, 35)};
    }
    return ticket;
}

constexpr const char* TicketSelect = R"SQL(
SELECT
    i.id, i.ticket_key, i.ticket_number,
    p.project_key, p.name,
    i.summary, i.description,
    it.type_key, it.name, it.icon, it.color,
    s.status_key, s.name, s.category, s.sort_order,
    pr.priority_key, pr.name, pr.rank, pr.color,
    reporter.id, reporter.display_name, reporter.email,
    assignee.id, assignee.display_name, assignee.email,
    parent.ticket_key,
    i.story_points, i.due_date::text,
    labels.names,
    i.created_at::text, i.updated_at::text, i.version, i.resolution, i.rank_order,
    comp.id, comp.name
FROM tickets i
JOIN projects p ON p.id = i.project_id
JOIN ticket_types it ON it.id = i.ticket_type_id
JOIN ticket_statuses s ON s.id = i.status_id
JOIN priorities pr ON pr.id = i.priority_id
JOIN users reporter ON reporter.id = i.reporter_user_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
LEFT JOIN tickets parent ON parent.id = i.parent_ticket_id
LEFT JOIN LATERAL (
    SELECT COALESCE(string_agg(l.name, ',' ORDER BY l.name), '') AS names
    FROM ticket_labels il JOIN labels l ON l.id = il.label_id
    WHERE il.ticket_id = i.id
) labels ON TRUE
LEFT JOIN project_components comp ON comp.id = i.component_id
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

// Components are named uniquely per project, not globally (D19), so the
// lookup must be scoped by projectId -- unlike lookupId's plain global
// key/value lookup used for ticket types/statuses/priorities.
std::string lookupComponentId(PGconn* connection, const std::string& projectId, const std::string& name) {
    auto result = execParams(connection,
                             "SELECT id FROM project_components WHERE project_id = $1 AND name = $2",
                             {projectId, name},
                             "Lookup component");
    if (PQntuples(result.get()) != 1) {
        throw std::invalid_argument("Unknown component: " + name);
    }
    return value(result.get(), 0, 0);
}

Domain::ProjectComponent readComponent(PGresult* result, int row) {
    Domain::ProjectComponent component;
    component.id = value(result, row, 0);
    component.projectKey = value(result, row, 1);
    component.name = value(result, row, 2);
    component.description = value(result, row, 3);
    if (PQgetisnull(result, row, 4) == 0) {
        component.lead = readUserSummary(result, row, 4);
    }
    if (PQgetisnull(result, row, 7) == 0) {
        component.defaultAssignee = readUserSummary(result, row, 7);
    }
    component.createdAt = value(result, row, 10);
    component.updatedAt = value(result, row, 11);
    return component;
}

constexpr const char* ComponentSelect = R"SQL(
SELECT c.id, p.project_key, c.name, c.description,
       lead.id, lead.display_name, lead.email,
       def.id, def.display_name, def.email,
       c.created_at::text, c.updated_at::text
FROM project_components c
JOIN projects p ON p.id = c.project_id
LEFT JOIN users lead ON lead.id = c.lead_user_id
LEFT JOIN users def ON def.id = c.default_assignee_user_id
)SQL";

// Custom fields (D9). `options` is stored as a small self-contained JSON
// array of strings -- a private encoding local to this adapter (not a
// dependency on the web layer's crow::json), matching SqliteDatabase's own
// identical encode/decode pair.
std::string encodeCustomFieldOptions(const std::vector<std::string>& options) {
    std::string json = "[";
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "\"";
        for (const char c : options[i]) {
            if (c == '"' || c == '\\') {
                json += '\\';
            }
            json += c;
        }
        json += "\"";
    }
    json += "]";
    return json;
}

std::vector<std::string> decodeCustomFieldOptions(const std::string& json) {
    std::vector<std::string> options;
    std::string current;
    bool inString = false;
    bool escape = false;
    for (const char c : json) {
        if (!inString) {
            if (c == '"') {
                inString = true;
                current.clear();
            }
            continue;
        }
        if (escape) {
            current += c;
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            inString = false;
            options.push_back(current);
            continue;
        }
        current += c;
    }
    return options;
}

constexpr const char* CustomFieldSelect = R"SQL(
SELECT f.id, p.project_key, f.name, f.field_type, f.options, f.required, f.sort_order, f.created_at::text
FROM custom_fields f
JOIN projects p ON p.id = f.project_id
)SQL";

Domain::CustomFieldDefinition readCustomFieldDefinition(PGresult* result, int row) {
    Domain::CustomFieldDefinition field;
    field.id = value(result, row, 0);
    field.projectKey = value(result, row, 1);
    field.name = value(result, row, 2);
    field.fieldType = value(result, row, 3);
    field.options = decodeCustomFieldOptions(value(result, row, 4));
    field.required = boolValue(result, row, 5);
    field.sortOrder = std::stoi(value(result, row, 6));
    field.createdAt = value(result, row, 7);
    return field;
}

std::string lookupTicketId(PGconn* connection, const std::string& ticketKey) {
    auto result = execParams(connection, R"SQL(
SELECT i.id
FROM tickets i
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))
)SQL", {ticketKey}, "Lookup ticket");
    if (PQntuples(result.get()) != 1) {
        throw std::invalid_argument("Unknown ticket key: " + ticketKey);
    }
    return value(result.get(), 0, 0);
}

Domain::Comment readComment(PGresult* result, int row) {
    Domain::Comment comment;
    comment.id = value(result, row, 0);
    comment.ticketId = value(result, row, 1);
    comment.author = readUserSummary(result, row, 2);
    comment.body = value(result, row, 5);
    comment.createdAt = value(result, row, 6);
    comment.updatedAt = value(result, row, 7);
    comment.version = int64Value(result, row, 8);
    comment.editedAt = optionalValue(result, row, 9);
    return comment;
}

constexpr const char* CommentSelect = R"SQL(
SELECT c.id, c.ticket_id, u.id, u.display_name, u.email,
       c.body, c.created_at::text, c.updated_at::text, c.version, c.edited_at::text
FROM comments c JOIN users u ON u.id = c.author_user_id
)SQL";

Domain::TicketHistoryEntry readTicketHistoryEntry(PGresult* result, int row) {
    Domain::TicketHistoryEntry entry;
    entry.id = value(result, row, 0);
    entry.ticketId = value(result, row, 1);
    if (PQgetisnull(result, row, 2) == 0) {
        entry.actor = readUserSummary(result, row, 2);
    }
    entry.fieldName = value(result, row, 5);
    entry.oldValue = optionalValue(result, row, 6);
    entry.newValue = optionalValue(result, row, 7);
    entry.createdAt = value(result, row, 8);
    return entry;
}

// LEFT JOIN (not JOIN, unlike CommentSelect/WorklogSelect above) --
// actor_user_id is nullable (ON DELETE SET NULL), so a since-deleted
// user's past history rows must still resolve instead of vanishing from
// the JOIN entirely.
constexpr const char* TicketHistorySelect = R"SQL(
SELECT h.id, h.ticket_id, u.id, u.display_name, u.email,
       h.field_name, h.old_value, h.new_value, h.created_at::text
FROM ticket_history h LEFT JOIN users u ON u.id = h.actor_user_id
)SQL";

Domain::Worklog readWorklog(PGresult* result, int row) {
    Domain::Worklog worklog;
    worklog.id = value(result, row, 0);
    worklog.ticketId = value(result, row, 1);
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
SELECT w.id, w.ticket_id, u.id, u.display_name, u.email,
       w.work_date::text, w.time_spent_seconds, w.comment, w.created_at::text, w.updated_at::text, w.version
FROM worklogs w JOIN users u ON u.id = w.author_user_id
)SQL";

Domain::Attachment readAttachment(PGresult* result, int row) {
    Domain::Attachment attachment;
    attachment.id = value(result, row, 0);
    attachment.ticketId = value(result, row, 1);
    attachment.uploader = readUserSummary(result, row, 2);
    attachment.fileName = value(result, row, 5);
    attachment.contentType = value(result, row, 6);
    attachment.byteSize = int64Value(result, row, 7);
    attachment.sha256 = value(result, row, 8);
    attachment.createdAt = value(result, row, 9);
    attachment.deletedAt = optionalValue(result, row, 10);
    attachment.ticketKey = value(result, row, 11);
    return attachment;
}

constexpr const char* AttachmentSelect = R"SQL(
SELECT a.id, a.ticket_id, u.id, u.display_name, u.email,
       a.file_name, a.content_type, a.byte_size, a.sha256, a.created_at::text, a.deleted_at::text, i.ticket_key
FROM attachments a JOIN users u ON u.id = a.uploader_user_id JOIN tickets i ON i.id = a.ticket_id
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
FROM board_columns bc JOIN ticket_statuses s ON s.id = bc.status_id
ORDER BY bc.sort_order
)SQL";

// Backup/restore (D106-D108) shells out to the pg_dump/psql binaries rather
// than reimplementing dump/restore over libpq -- both are standard
// PostgreSQL client tools expected to be present alongside any Postgres
// deployment, and re-deriving their logic would be substantial, fragile
// duplication for no real benefit. `connectionString_` and the backup/
// restore directory are both admin-controlled (from `TICKETHUB_DATABASE_URL`
// and a CLI argument), not untrusted network input, but single-quoting is
// still applied for correctness against paths/values containing shell
// metacharacters or spaces.
std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
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

// `--clean --if-exists` makes the dump self-contained for a direct restore
// (D108: "direct restore into the target database ... no isolated staging
// environment") -- the dump itself drops each object before recreating it,
// so replaying it against a non-empty target (e.g. restoring the same
// backup twice, or restoring over an existing installation) works without
// requiring the admin to manually drop/recreate the database first.
void PostgresDatabase::backup(const std::string& directory) {
    std::filesystem::create_directories(directory);
    const auto outputPath = (std::filesystem::path(directory) / "database.sql").string();
    const std::string command = "pg_dump --clean --if-exists " + shellQuote(connectionString_)
        + " -f " + shellQuote(outputPath);
    if (std::system(command.c_str()) != 0) {
        throw std::runtime_error("pg_dump failed -- see stderr above for detail");
    }
}

// `-v ON_ERROR_STOP=1` makes psql abort (non-zero exit) on the first SQL
// error instead of continuing and reporting success -- without it, a
// partially-failed restore could silently leave the database in a mixed
// state while still looking like it succeeded.
void PostgresDatabase::restore(const std::string& directory) {
    const auto inputPath = std::filesystem::path(directory) / "database.sql";
    if (!std::filesystem::exists(inputPath)) {
        throw std::runtime_error("database.sql not found in backup directory: " + directory);
    }
    const std::string command = "psql -v ON_ERROR_STOP=1 " + shellQuote(connectionString_)
        + " -f " + shellQuote(inputPath.string());
    if (std::system(command.c_str()) != 0) {
        throw std::runtime_error("psql restore failed -- see stderr above for detail");
    }
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

void PostgresDatabase::updateUserPreferences(const std::string& userId, const Domain::UpdatePreferencesRequest& request) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), R"SQL(
UPDATE users SET time_zone = $1, clock_format = $2, updated_at = CURRENT_TIMESTAMP WHERE id = $3
)SQL",
               {request.timeZone, request.clockFormat, userId}, "Update user preferences");
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
       counts.ticket_count, counts.open_ticket_count,
       p.archived
FROM projects p
LEFT JOIN users lead ON lead.id = p.lead_user_id
LEFT JOIN LATERAL (
    SELECT COUNT(i.id) AS ticket_count,
           COUNT(i.id) FILTER (WHERE s.category <> 'done') AS open_ticket_count
    FROM tickets i JOIN ticket_statuses s ON s.id = i.status_id
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
    project.ticketCount = int64Value(result, row, 7);
    project.openTicketCount = int64Value(result, row, 8);
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
INSERT INTO projects(id, project_key, name, description, lead_user_id, next_ticket_number, archived, created_at, updated_at)
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

std::optional<Domain::Project> PostgresDatabase::changeProjectKey(const std::string& oldKey, const std::string& newKey) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin change project key transaction");
    try {
        auto projectRow = execParams(connection.get(), "SELECT id FROM projects WHERE project_key = $1 AND deleted_at IS NULL",
                                     {oldKey}, "Find project by key");
        if (PQntuples(projectRow.get()) != 1) {
            exec(connection.get(), "ROLLBACK", "Rollback missing project transaction");
            return std::nullopt;
        }
        const std::string projectId = value(projectRow.get(), 0, 0);

        auto collision = execParams(connection.get(), R"SQL(
SELECT 1 FROM projects WHERE project_key = $1 AND deleted_at IS NULL
UNION ALL
SELECT 1 FROM project_key_aliases WHERE alias_key = $1
)SQL",
                                    {newKey}, "Check new project key collision");
        if (PQntuples(collision.get()) != 0) {
            throw std::invalid_argument("Project key is already in use: " + newKey);
        }

        // The old key becomes a permanent alias -- same mechanism moveTicket
        // already established for ticket_key_aliases (D38).
        execParams(connection.get(), "INSERT INTO project_key_aliases(alias_key, project_id) VALUES ($1, $2)",
                  {oldKey, projectId}, "Insert project key alias");

        execParams(connection.get(), "UPDATE projects SET project_key = $1, updated_at = CURRENT_TIMESTAMP WHERE id = $2",
                  {newKey, projectId}, "Update project key");

        // Every ticket ever created under this project -- including
        // soft-deleted ones, since a key must stay permanently resolvable
        // regardless of the ticket's own lifecycle state -- is renamed to
        // the new prefix with the same numeric suffix; its own old key
        // becomes a ticket_key_aliases entry, mirroring moveTicket's
        // single-ticket case.
        auto tickets = execParams(connection.get(), "SELECT id, ticket_number, ticket_key FROM tickets WHERE project_id = $1",
                                  {projectId}, "List tickets for key rename");
        for (int row = 0; row < PQntuples(tickets.get()); ++row) {
            const std::string ticketId = value(tickets.get(), row, 0);
            const std::string ticketNumber = value(tickets.get(), row, 1);
            const std::string oldTicketKey = value(tickets.get(), row, 2);
            const std::string newTicketKey = newKey + "-" + ticketNumber;

            execParams(connection.get(), "INSERT INTO ticket_key_aliases(alias_key, ticket_id) VALUES ($1, $2)",
                      {oldTicketKey, ticketId}, "Insert ticket key alias");
            execParams(connection.get(), "UPDATE tickets SET ticket_key = $1 WHERE id = $2",
                      {newTicketKey, ticketId}, "Rename ticket key");
        }

        exec(connection.get(), "COMMIT", "Commit change project key transaction");

        auto result = execParams(connection.get(), std::string(ProjectSelectSql) + " WHERE p.id = $1",
                                 {projectId}, "Read renamed project");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Renamed project could not be read back");
        }
        return readProject(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback change project key transaction");
        } catch (...) {
        }
        throw;
    }
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

// --- Project components (D19) ---

std::vector<Domain::ProjectComponent> PostgresDatabase::listComponents(const std::string& projectKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(),
                             std::string(ComponentSelect) + " WHERE p.project_key = $1 ORDER BY c.name",
                             {projectKey}, "List components");
    std::vector<Domain::ProjectComponent> components;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        components.push_back(readComponent(result.get(), row));
    }
    return components;
}

Domain::ProjectComponent PostgresDatabase::createComponent(const Domain::CreateComponentRequest& request) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin create component transaction");
    try {
        const std::string projectId = lookupId(connection.get(), "projects", "project_key", request.projectKey);
        std::optional<std::string> leadId;
        if (request.leadEmail.has_value() && !request.leadEmail->empty()) {
            leadId = lookupId(connection.get(), "users", "email", *request.leadEmail);
        }
        std::optional<std::string> defaultAssigneeId;
        if (request.defaultAssigneeEmail.has_value() && !request.defaultAssigneeEmail->empty()) {
            defaultAssigneeId = lookupId(connection.get(), "users", "email", *request.defaultAssigneeEmail);
        }

        auto existing = execParams(connection.get(), "SELECT 1 FROM project_components WHERE project_id = $1 AND name = $2",
                                   {projectId, request.name}, "Check existing component name");
        if (PQntuples(existing.get()) != 0) {
            throw std::invalid_argument("Component name is already in use in this project: " + request.name);
        }

        const std::string componentId = Common::uuidV4();
        execParams(connection.get(), R"SQL(
INSERT INTO project_components(id, project_id, name, description, lead_user_id, default_assignee_user_id, created_at, updated_at)
VALUES ($1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
                   {componentId, projectId, request.name, request.description, leadId, defaultAssigneeId},
                   "Insert component");

        exec(connection.get(), "COMMIT", "Commit create component transaction");

        auto result = execParams(connection.get(), std::string(ComponentSelect) + " WHERE c.id = $1",
                                 {componentId}, "Read created component");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Created component could not be read back");
        }
        return readComponent(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback create component transaction");
        } catch (...) {
        }
        throw;
    }
}

std::optional<Domain::ProjectComponent> PostgresDatabase::findComponentById(const std::string& componentId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(ComponentSelect) + " WHERE c.id = $1",
                             {componentId}, "Find component");
    if (PQntuples(result.get()) != 1) {
        return std::nullopt;
    }
    return readComponent(result.get(), 0);
}

std::optional<Domain::ProjectComponent> PostgresDatabase::editComponent(const std::string& componentId,
                                                                          const Domain::EditComponentRequest& request) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin edit component transaction");
    try {
        auto existing = execParams(connection.get(), "SELECT project_id FROM project_components WHERE id = $1",
                                   {componentId}, "Find component for edit");
        if (PQntuples(existing.get()) != 1) {
            exec(connection.get(), "ROLLBACK", "Rollback edit component transaction");
            return std::nullopt;
        }
        const std::string projectId = value(existing.get(), 0, 0);

        std::optional<std::string> leadId;
        if (request.leadEmail.has_value() && !request.leadEmail->empty()) {
            leadId = lookupId(connection.get(), "users", "email", *request.leadEmail);
        }
        std::optional<std::string> defaultAssigneeId;
        if (request.defaultAssigneeEmail.has_value() && !request.defaultAssigneeEmail->empty()) {
            defaultAssigneeId = lookupId(connection.get(), "users", "email", *request.defaultAssigneeEmail);
        }

        auto nameClash = execParams(connection.get(),
                                    "SELECT 1 FROM project_components WHERE project_id = $1 AND name = $2 AND id <> $3",
                                    {projectId, request.name, componentId}, "Check component name clash");
        if (PQntuples(nameClash.get()) != 0) {
            throw std::invalid_argument("Component name is already in use in this project: " + request.name);
        }

        execParams(connection.get(), R"SQL(
UPDATE project_components
SET name = $1, description = $2, lead_user_id = $3, default_assignee_user_id = $4, updated_at = CURRENT_TIMESTAMP
WHERE id = $5
)SQL",
                   {request.name, request.description, leadId, defaultAssigneeId, componentId},
                   "Update component");

        exec(connection.get(), "COMMIT", "Commit edit component transaction");

        auto result = execParams(connection.get(), std::string(ComponentSelect) + " WHERE c.id = $1",
                                 {componentId}, "Read edited component");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Edited component could not be read back");
        }
        return readComponent(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback edit component transaction");
        } catch (...) {
        }
        throw;
    }
}

bool PostgresDatabase::deleteComponent(const std::string& componentId) {
    auto connection = connect(connectionString_);
    // No recycle bin (D19 does not call for one, unlike tickets/projects) --
    // any ticket referencing this component has it cleared via the
    // component_id column's ON DELETE SET NULL, not rejected or cascaded.
    auto result = execParams(connection.get(), "DELETE FROM project_components WHERE id = $1",
                             {componentId}, "Delete component");
    return std::string(PQcmdTuples(result.get())) != "0";
}

// --- Custom fields (D9, deferred-after-V1) ---

std::vector<Domain::CustomFieldDefinition> PostgresDatabase::listCustomFields(const std::string& projectKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(),
                             std::string(CustomFieldSelect) + " WHERE p.project_key = $1 ORDER BY f.sort_order, f.name",
                             {projectKey}, "List custom fields");
    std::vector<Domain::CustomFieldDefinition> fields;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        fields.push_back(readCustomFieldDefinition(result.get(), row));
    }
    return fields;
}

Domain::CustomFieldDefinition PostgresDatabase::createCustomField(const Domain::CreateCustomFieldRequest& request) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin create custom field transaction");
    try {
        const std::string projectId = lookupId(connection.get(), "projects", "project_key", request.projectKey);

        auto existing = execParams(connection.get(), "SELECT 1 FROM custom_fields WHERE project_id = $1 AND name = $2",
                                   {projectId, request.name}, "Check existing custom field name");
        if (PQntuples(existing.get()) != 0) {
            throw std::invalid_argument("Custom field name is already in use in this project: " + request.name);
        }

        auto maxOrder = execParams(connection.get(), "SELECT COALESCE(MAX(sort_order), -1) FROM custom_fields WHERE project_id = $1",
                                   {projectId}, "Find next custom field sort order");
        const int nextOrder = std::stoi(value(maxOrder.get(), 0, 0)) + 1;

        const std::string fieldId = Common::uuidV4();
        execParams(connection.get(), R"SQL(
INSERT INTO custom_fields(id, project_id, name, field_type, options, required, sort_order, created_at)
VALUES ($1, $2, $3, $4, $5, $6, $7, CURRENT_TIMESTAMP)
)SQL",
                   {fieldId, projectId, request.name, request.fieldType, encodeCustomFieldOptions(request.options),
                    std::string(request.required ? "true" : "false"), std::to_string(nextOrder)},
                   "Insert custom field");

        exec(connection.get(), "COMMIT", "Commit create custom field transaction");

        auto result = execParams(connection.get(), std::string(CustomFieldSelect) + " WHERE f.id = $1",
                                 {fieldId}, "Read created custom field");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Created custom field could not be read back");
        }
        return readCustomFieldDefinition(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback create custom field transaction");
        } catch (...) {
        }
        throw;
    }
}

std::optional<Domain::CustomFieldDefinition> PostgresDatabase::findCustomFieldById(const std::string& fieldId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(CustomFieldSelect) + " WHERE f.id = $1",
                             {fieldId}, "Find custom field");
    if (PQntuples(result.get()) != 1) {
        return std::nullopt;
    }
    return readCustomFieldDefinition(result.get(), 0);
}

std::optional<Domain::CustomFieldDefinition> PostgresDatabase::editCustomField(const std::string& fieldId,
                                                                                const Domain::EditCustomFieldRequest& request) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin edit custom field transaction");
    try {
        auto existing = execParams(connection.get(), "SELECT project_id FROM custom_fields WHERE id = $1",
                                   {fieldId}, "Find custom field for edit");
        if (PQntuples(existing.get()) != 1) {
            exec(connection.get(), "ROLLBACK", "Rollback edit custom field transaction");
            return std::nullopt;
        }
        const std::string projectId = value(existing.get(), 0, 0);

        auto nameClash = execParams(connection.get(),
                                    "SELECT 1 FROM custom_fields WHERE project_id = $1 AND name = $2 AND id <> $3",
                                    {projectId, request.name, fieldId}, "Check custom field name clash");
        if (PQntuples(nameClash.get()) != 0) {
            throw std::invalid_argument("Custom field name is already in use in this project: " + request.name);
        }

        execParams(connection.get(), R"SQL(
UPDATE custom_fields SET name = $1, options = $2, required = $3, sort_order = $4 WHERE id = $5
)SQL",
                   {request.name, encodeCustomFieldOptions(request.options),
                    std::string(request.required ? "true" : "false"), std::to_string(request.sortOrder), fieldId},
                   "Update custom field");

        exec(connection.get(), "COMMIT", "Commit edit custom field transaction");

        auto result = execParams(connection.get(), std::string(CustomFieldSelect) + " WHERE f.id = $1",
                                 {fieldId}, "Read edited custom field");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Edited custom field could not be read back");
        }
        return readCustomFieldDefinition(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback edit custom field transaction");
        } catch (...) {
        }
        throw;
    }
}

bool PostgresDatabase::deleteCustomField(const std::string& fieldId) {
    auto connection = connect(connectionString_);
    // No recycle bin (matches project components, D19) -- every stored
    // value for this field is cascaded away via ticket_custom_field_values'
    // ON DELETE CASCADE on field_id.
    auto result = execParams(connection.get(), "DELETE FROM custom_fields WHERE id = $1",
                             {fieldId}, "Delete custom field");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::CustomFieldValue> PostgresDatabase::listTicketCustomFieldValues(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    // One row per field defined on the ticket's project, whether or not a
    // value has ever been stored for it (LEFT JOIN), so the caller can
    // render every field -- including unset ones -- on the ticket view.
    auto result = execParams(connection.get(), R"SQL(
SELECT f.id, f.name, f.field_type, v.value
FROM custom_fields f
JOIN tickets i ON i.project_id = f.project_id
LEFT JOIN ticket_custom_field_values v ON v.field_id = f.id AND v.ticket_id = i.id
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))
ORDER BY f.sort_order, f.name
)SQL",
                             {ticketKey}, "List ticket custom field values");
    std::vector<Domain::CustomFieldValue> values;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        Domain::CustomFieldValue field;
        field.fieldId = value(result.get(), row, 0);
        field.name = value(result.get(), row, 1);
        field.fieldType = value(result.get(), row, 2);
        if (PQgetisnull(result.get(), row, 3) == 0) {
            field.value = value(result.get(), row, 3);
        }
        values.push_back(std::move(field));
    }
    return values;
}

namespace {
// Called only from within an already-open transaction (createTicket/
// editTicket, on their own already-open `connection`), matching how they
// already handle labels/component inline -- a free function taking the
// live PGconn*, matching lookupId's existing pattern for this kind of
// internal helper. Anonymous-namespace-scoped (internal linkage) since
// SqliteDatabase.cpp defines its own same-named equivalent for its own
// sqlite3 type.
void applyTicketCustomFieldValues(PGconn* connection, const std::string& ticketId, const std::string& projectId,
                                   const std::vector<Domain::CustomFieldValueInput>& values) {
    execParams(connection, "DELETE FROM ticket_custom_field_values WHERE ticket_id = $1",
              {ticketId}, "Clear ticket custom field values");
    for (const auto& input : values) {
        if (input.value.empty()) {
            continue;
        }
        // Scopes field_id to this ticket's own project -- a fieldId that
        // exists but belongs to a different project matches no row, so
        // nothing is inserted and the RETURNING check below rejects it the
        // same way an entirely unknown id would.
        auto result = execParams(connection, R"SQL(
INSERT INTO ticket_custom_field_values(ticket_id, field_id, value)
SELECT $1, id, $2 FROM custom_fields WHERE id = $3 AND project_id = $4
RETURNING ticket_id
)SQL",
                                 {ticketId, input.value, input.fieldId, projectId},
                                 "Insert ticket custom field value");
        if (PQntuples(result.get()) == 0) {
            throw std::invalid_argument("Unknown custom field id for this project: " + input.fieldId);
        }
    }
}
} // namespace

// --- Ticket tracker ---

std::vector<Domain::Project> PostgresDatabase::listProjects() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), R"SQL(
SELECT p.id, p.project_key, p.name, p.description,
       lead.id, lead.display_name, lead.email,
       counts.ticket_count, counts.open_ticket_count
FROM projects p
LEFT JOIN users lead ON lead.id = p.lead_user_id
LEFT JOIN LATERAL (
    SELECT COUNT(i.id) AS ticket_count,
           COUNT(i.id) FILTER (WHERE s.category <> 'done') AS open_ticket_count
    FROM tickets i JOIN ticket_statuses s ON s.id = i.status_id
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
        project.ticketCount = int64Value(result.get(), row, 7);
        project.openTicketCount = int64Value(result.get(), row, 8);
        projects.push_back(std::move(project));
    }
    return projects;
}

std::vector<Domain::Ticket> PostgresDatabase::listTickets(const Domain::TicketFilter& filter) {
    auto connection = connect(connectionString_);
    // `label` is checked via EXISTS rather than the already-aggregated `labels`
    // LATERAL join (which feeds the label-list summary column) -- filtering on
    // the joined row directly would restrict that aggregate to only the
    // matching label instead of the ticket's full label list.
    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND ($1::text IS NULL OR p.project_key = $1)
  AND ($2::text IS NULL OR s.status_key = $2)
  AND ($3::text IS NULL OR it.type_key = $3)
  AND ($4::text IS NULL OR pr.priority_key = $4)
  AND ($5::text IS NULL OR assignee.email = $5)
  AND ($6::text IS NULL OR i.due_date <= $6::date)
  AND ($7::text IS NULL OR EXISTS (
        SELECT 1 FROM ticket_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.ticket_id = i.id AND l2.name ILIKE $7))
  AND ($8::text IS NULL OR i.summary ILIKE $8 OR i.description ILIKE $8 OR i.ticket_key ILIKE $8)
  AND ($9::text IS NULL OR comp.name ILIKE $9)
)SQL" + (filter.sortByRank ? "ORDER BY i.rank_order, i.ticket_number\n" : "ORDER BY i.updated_at DESC, i.ticket_key DESC\n") + R"SQL(
LIMIT 200
)SQL";
    auto result = execParams(connection.get(),
                             sql,
                             {filter.projectKey,
                              filter.statusKey,
                              filter.ticketTypeKey,
                              filter.priorityKey,
                              filter.assigneeEmail,
                              filter.dueBefore,
                              filter.label,
                              filter.search ? std::optional<std::string>("%" + *filter.search + "%") : std::nullopt,
                              filter.componentName},
                             "List tickets");
    std::vector<Domain::Ticket> tickets;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        tickets.push_back(readTicket(result.get(), row));
    }
    return tickets;
}

std::vector<Domain::Ticket> PostgresDatabase::listTickets(const Domain::TicketFilter& filter, int limit, int offset) {
    auto connection = connect(connectionString_);
    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND ($1::text IS NULL OR p.project_key = $1)
  AND ($2::text IS NULL OR s.status_key = $2)
  AND ($3::text IS NULL OR it.type_key = $3)
  AND ($4::text IS NULL OR pr.priority_key = $4)
  AND ($5::text IS NULL OR assignee.email = $5)
  AND ($6::text IS NULL OR i.due_date <= $6::date)
  AND ($7::text IS NULL OR EXISTS (
        SELECT 1 FROM ticket_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.ticket_id = i.id AND l2.name ILIKE $7))
  AND ($8::text IS NULL OR i.summary ILIKE $8 OR i.description ILIKE $8 OR i.ticket_key ILIKE $8)
  AND ($9::text IS NULL OR comp.name ILIKE $9)
)SQL" + (filter.sortByRank ? "ORDER BY i.rank_order, i.ticket_number\n" : "ORDER BY i.updated_at DESC, i.ticket_key DESC\n") + R"SQL(
LIMIT $10::int OFFSET $11::int
)SQL";
    auto result = execParams(connection.get(),
                             sql,
                             {filter.projectKey,
                              filter.statusKey,
                              filter.ticketTypeKey,
                              filter.priorityKey,
                              filter.assigneeEmail,
                              filter.dueBefore,
                              filter.label,
                              filter.search ? std::optional<std::string>("%" + *filter.search + "%") : std::nullopt,
                              filter.componentName,
                              std::optional<std::string>(std::to_string(limit)),
                              std::optional<std::string>(std::to_string(offset))},
                             "List tickets (paginated)");
    std::vector<Domain::Ticket> tickets;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        tickets.push_back(readTicket(result.get(), row));
    }
    return tickets;
}

std::int64_t PostgresDatabase::countTickets(const Domain::TicketFilter& filter) {
    auto connection = connect(connectionString_);
    const std::string sql = R"SQL(
SELECT COUNT(*)
FROM tickets i
JOIN projects p ON p.id = i.project_id
JOIN ticket_types it ON it.id = i.ticket_type_id
JOIN ticket_statuses s ON s.id = i.status_id
JOIN priorities pr ON pr.id = i.priority_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
LEFT JOIN project_components comp ON comp.id = i.component_id
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND ($1::text IS NULL OR p.project_key = $1)
  AND ($2::text IS NULL OR s.status_key = $2)
  AND ($3::text IS NULL OR it.type_key = $3)
  AND ($4::text IS NULL OR pr.priority_key = $4)
  AND ($5::text IS NULL OR assignee.email = $5)
  AND ($6::text IS NULL OR i.due_date <= $6::date)
  AND ($7::text IS NULL OR EXISTS (
        SELECT 1 FROM ticket_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.ticket_id = i.id AND l2.name ILIKE $7))
  AND ($8::text IS NULL OR i.summary ILIKE $8 OR i.description ILIKE $8 OR i.ticket_key ILIKE $8)
  AND ($9::text IS NULL OR comp.name ILIKE $9)
)SQL";
    auto result = execParams(connection.get(),
                             sql,
                             {filter.projectKey,
                              filter.statusKey,
                              filter.ticketTypeKey,
                              filter.priorityKey,
                              filter.assigneeEmail,
                              filter.dueBefore,
                              filter.label,
                              filter.search ? std::optional<std::string>("%" + *filter.search + "%") : std::nullopt,
                              filter.componentName},
                             "Count tickets");
    if (PQntuples(result.get()) == 0) {
        return 0;
    }
    return int64Value(result.get(), 0, 0);
}

std::optional<Domain::Ticket> PostgresDatabase::findTicketByKey(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))", {ticketKey}, "Find ticket");
    if (PQntuples(result.get()) == 0) {
        return std::nullopt;
    }
    return readTicket(result.get(), 0);
}

Domain::Ticket PostgresDatabase::createTicket(const Domain::CreateTicketRequest& request,
                                            const std::string& reporterUserId) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin create ticket transaction");
    try {
        auto project = execParams(connection.get(),
                                  "SELECT id, next_ticket_number FROM projects WHERE project_key = $1 AND archived = FALSE AND deleted_at IS NULL FOR UPDATE",
                                  {request.projectKey},
                                  "Lock project");
        if (PQntuples(project.get()) != 1) {
            throw std::invalid_argument("Unknown project: " + request.projectKey);
        }
        const std::string projectId = value(project.get(), 0, 0);
        const std::int64_t ticketNumber = int64Value(project.get(), 0, 1);
        const std::string ticketKey = request.projectKey + "-" + std::to_string(ticketNumber);

        execParams(connection.get(),
                   "UPDATE projects SET next_ticket_number = next_ticket_number + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $1",
                   {projectId},
                   "Increment project ticket counter");

        const std::string ticketId = Common::uuidV4();
        const std::string ticketTypeId = lookupId(connection.get(), "ticket_types", "type_key", request.ticketTypeKey);
        const std::string statusId = lookupId(connection.get(), "ticket_statuses", "status_key", "backlog");
        const std::string priorityId = lookupId(connection.get(), "priorities", "priority_key", request.priorityKey);
        const std::string reporterId = requireUserId(connection.get(), reporterUserId);
        std::optional<std::string> assigneeId;
        if (request.assigneeEmail && !request.assigneeEmail->empty()) {
            assigneeId = lookupId(connection.get(), "users", "email", *request.assigneeEmail);
        }
        std::optional<std::string> parentId;
        if (request.parentTicketKey && !request.parentTicketKey->empty()) {
            parentId = lookupTicketId(connection.get(), *request.parentTicketKey);
        }
        std::optional<std::string> componentId;
        if (request.componentName && !request.componentName->empty()) {
            componentId = lookupComponentId(connection.get(), projectId, *request.componentName);
        }

        // Simple integer manual order (D31): new tickets are appended after
        // the highest existing rank within their project. The project row is
        // already FOR-UPDATE-locked above, which serializes this alongside
        // concurrent creates in the same project.
        auto maxRank = execParams(connection.get(),
                                  "SELECT COALESCE(MAX(rank_order), 0) + 1 FROM tickets WHERE project_id = $1 AND deleted_at IS NULL",
                                  {projectId},
                                  "Compute next rank order");
        const std::int64_t rankOrder = int64Value(maxRank.get(), 0, 0);

        execParams(connection.get(), R"SQL(
INSERT INTO tickets(id, project_id, ticket_number, ticket_key, summary, description,
                   ticket_type_id, status_id, priority_id, reporter_user_id, assignee_user_id,
                   parent_ticket_id, story_points, due_date, rank_order, component_id, created_at, updated_at)
VALUES ($1, $2, $3::bigint, $4, $5, $6, $7, $8, $9, $10, $11,
        $12, $13::double precision, $14::date, $15::bigint, $16, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
                   {ticketId,
                    projectId,
                    std::to_string(ticketNumber),
                    ticketKey,
                    request.summary,
                    request.description,
                    ticketTypeId,
                    statusId,
                    priorityId,
                    reporterId,
                    assigneeId,
                    parentId,
                    request.storyPoints ? std::optional<std::string>(std::to_string(*request.storyPoints)) : std::nullopt,
                    request.dueDate,
                    std::to_string(rankOrder),
                    componentId},
                   "Insert ticket");

        for (const auto& labelName : request.labels) {
            execParams(connection.get(),
                       "INSERT INTO labels(id, name) VALUES ($1, $2) ON CONFLICT(name) DO NOTHING",
                       {Common::uuidV4(), labelName},
                       "Insert label");
            execParams(connection.get(), R"SQL(
INSERT INTO ticket_labels(ticket_id, label_id)
SELECT $1, id FROM labels WHERE name = $2
ON CONFLICT DO NOTHING
)SQL",
                       {ticketId, labelName},
                       "Link label");
        }

        applyTicketCustomFieldValues(connection.get(), ticketId, projectId, request.customFieldValues);

        exec(connection.get(), "COMMIT", "Commit create ticket transaction");
        auto result = execParams(connection.get(), std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))", {ticketKey}, "Read created ticket");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Created ticket could not be read back");
        }
        return readTicket(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback create ticket transaction");
        } catch (...) {
        }
        throw;
    }
}

bool PostgresDatabase::changeTicketStatus(const std::string& ticketKey,
                                         const std::string& statusKey,
                                         const std::string& actorUserId,
                                         const std::optional<std::string> resolution,
                                         const std::optional<std::int64_t> expectedVersion) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin status transaction");
    try {
        auto current = execParams(connection.get(), R"SQL(
SELECT i.id, s.status_key, s.category, i.version, i.resolution
FROM tickets i JOIN ticket_statuses s ON s.id = i.status_id
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))
FOR UPDATE OF i
)SQL",
                                  {ticketKey},
                                  "Lock ticket");
        if (PQntuples(current.get()) == 0) {
            exec(connection.get(), "ROLLBACK", "Rollback missing ticket transaction");
            return false;
        }
        const std::string ticketId = value(current.get(), 0, 0);
        const std::string oldStatus = value(current.get(), 0, 1);
        const std::string oldCategory = value(current.get(), 0, 2);
        const std::int64_t currentVersion = int64Value(current.get(), 0, 3);
        const bool hadResolution = PQgetisnull(current.get(), 0, 4) == 0;
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Ticket was modified by another user");
        }
        // A same-status call is normally a pure no-op (e.g. an idempotent
        // resubmit) -- except when the ticket is Done-category but somehow
        // has no recorded resolution yet (imported/historical data) and a
        // valid one is now being supplied. That's the one legitimate
        // "confirm a resolution retroactively" case, reachable through the
        // same resolution UI used for a real transition into Done; every
        // other same-status call remains a pure no-op, matching D68-D70's
        // "any other transition leaves resolution alone."
        const bool settingMissingResolution = oldCategory == "done" && !hadResolution && resolution && !resolution->empty();
        if (oldStatus == statusKey && !settingMissingResolution) {
            exec(connection.get(), "COMMIT", "Commit unchanged status transaction");
            return true;
        }

        auto targetStatus = execParams(connection.get(),
                                       "SELECT id, category FROM ticket_statuses WHERE status_key = $1",
                                       {statusKey},
                                       "Lookup target status");
        if (PQntuples(targetStatus.get()) != 1) {
            throw std::invalid_argument("Unknown ticket_statuses key: " + statusKey);
        }
        const std::string statusId = value(targetStatus.get(), 0, 0);
        const std::string targetCategory = value(targetStatus.get(), 0, 1);
        const std::string actorId = requireUserId(connection.get(), actorUserId);

        // The fixed workflow rules (D68-D70): completing a ticket requires a
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
FROM tickets child
JOIN ticket_statuses cs ON cs.id = child.status_id
WHERE child.parent_ticket_id = $1 AND child.deleted_at IS NULL AND cs.category <> 'done'
LIMIT 1
)SQL",
                                              {ticketId},
                                              "Check unfinished sub-tasks");
            if (PQntuples(unfinishedChild.get()) != 0) {
                throw Domain::WorkflowViolation("Cannot complete a ticket while it has unfinished sub-tasks");
            }
            touchResolution = true;
            resolutionValue = resolution;
        } else if (oldCategory == "done") {
            touchResolution = true;
            resolutionValue = std::nullopt;
        }

        if (touchResolution) {
            execParams(connection.get(),
                       "UPDATE tickets SET status_id = $1, resolution = $2, version = version + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $3",
                       {statusId, resolutionValue, ticketId},
                       "Update ticket status");
        } else {
            execParams(connection.get(),
                       "UPDATE tickets SET status_id = $1, version = version + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $2",
                       {statusId, ticketId},
                       "Update ticket status");
        }
        execParams(connection.get(), R"SQL(
INSERT INTO ticket_history(id, ticket_id, actor_user_id, field_name, old_value, new_value)
VALUES ($1, $2, $3, 'status', $4, $5)
)SQL",
                   {Common::uuidV4(), ticketId, actorId, oldStatus, statusKey},
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

std::optional<Domain::Ticket> PostgresDatabase::editTicket(const std::string& ticketKey,
                                                         const Domain::EditTicketRequest& request,
                                                         const std::string& actorUserId,
                                                         const std::optional<std::int64_t> expectedVersion) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin edit ticket transaction");
    try {
        auto current = execParams(connection.get(), R"SQL(
SELECT i.id, i.summary, i.description, pr.priority_key, assignee.email,
       i.story_points, i.due_date::text, i.version, it.type_key,
       (SELECT ticket_key FROM tickets WHERE id = i.parent_ticket_id),
       i.project_id, comp.name
FROM tickets i
JOIN priorities pr ON pr.id = i.priority_id
JOIN ticket_types it ON it.id = i.ticket_type_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
LEFT JOIN project_components comp ON comp.id = i.component_id
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))
FOR UPDATE OF i
)SQL",
                                  {ticketKey},
                                  "Lock ticket for edit");
        if (PQntuples(current.get()) == 0) {
            exec(connection.get(), "ROLLBACK", "Rollback missing ticket transaction");
            return std::nullopt;
        }
        const std::string ticketId = value(current.get(), 0, 0);
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
        const std::string oldTypeKey = value(current.get(), 0, 8);
        const std::optional<std::string> oldParentKey = optionalValue(current.get(), 0, 9);
        const std::string projectId = value(current.get(), 0, 10);
        const std::optional<std::string> oldComponentName = optionalValue(current.get(), 0, 11);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Ticket was modified by another user");
        }

        // See the SQLite adapter's editTicket for why this must be checked
        // transactionally rather than in TicketService.
        if (Domain::ticketTypeHierarchyLevel(oldTypeKey) != Domain::ticketTypeHierarchyLevel(request.ticketTypeKey)) {
            auto childCheck = execParams(connection.get(),
                                         "SELECT COUNT(*) FROM tickets WHERE parent_ticket_id = $1 AND deleted_at IS NULL",
                                         {ticketId},
                                         "Count child tickets");
            if (int64Value(childCheck.get(), 0, 0) > 0) {
                throw std::invalid_argument(
                    "Cannot change a ticket's type across hierarchy levels while it has child tickets");
            }
        }

        const std::string priorityId = lookupId(connection.get(), "priorities", "priority_key", request.priorityKey);
        std::optional<std::string> assigneeId;
        if (request.assigneeEmail && !request.assigneeEmail->empty()) {
            assigneeId = lookupId(connection.get(), "users", "email", *request.assigneeEmail);
        }
        const std::string ticketTypeId = lookupId(connection.get(), "ticket_types", "type_key", request.ticketTypeKey);
        std::optional<std::string> parentId;
        if (request.parentTicketKey && !request.parentTicketKey->empty()) {
            parentId = lookupTicketId(connection.get(), *request.parentTicketKey);
        }
        std::optional<std::string> componentId;
        if (request.componentName && !request.componentName->empty()) {
            componentId = lookupComponentId(connection.get(), projectId, *request.componentName);
        }
        const std::string actorId = requireUserId(connection.get(), actorUserId);

        execParams(connection.get(), R"SQL(
UPDATE tickets
SET summary = $1, description = $2, priority_id = $3, assignee_user_id = $4,
    ticket_type_id = $5, parent_ticket_id = $6,
    story_points = $7::double precision, due_date = $8::date, component_id = $9, version = version + 1, updated_at = CURRENT_TIMESTAMP
WHERE id = $10
)SQL",
                   {request.summary,
                    request.description,
                    priorityId,
                    assigneeId,
                    ticketTypeId,
                    parentId,
                    request.storyPoints ? std::optional<std::string>(std::to_string(*request.storyPoints)) : std::nullopt,
                    request.dueDate,
                    componentId,
                    ticketId},
                   "Update ticket fields");

        execParams(connection.get(), "DELETE FROM ticket_labels WHERE ticket_id = $1", {ticketId}, "Clear ticket labels");
        for (const auto& labelName : request.labels) {
            execParams(connection.get(),
                       "INSERT INTO labels(id, name) VALUES ($1, $2) ON CONFLICT(name) DO NOTHING",
                       {Common::uuidV4(), labelName},
                       "Insert label");
            execParams(connection.get(), R"SQL(
INSERT INTO ticket_labels(ticket_id, label_id)
SELECT $1, id FROM labels WHERE name = $2
ON CONFLICT DO NOTHING
)SQL",
                       {ticketId, labelName},
                       "Link label");
        }

        applyTicketCustomFieldValues(connection.get(), ticketId, projectId, request.customFieldValues);

        auto recordHistory = [&](const char* field, const std::string& oldValue, const std::string& newValue) {
            if (oldValue == newValue) {
                return;
            }
            std::optional<std::string> oldParam = oldValue.empty() ? std::nullopt : std::optional<std::string>(oldValue);
            std::optional<std::string> newParam = newValue.empty() ? std::nullopt : std::optional<std::string>(newValue);
            execParams(connection.get(), R"SQL(
INSERT INTO ticket_history(id, ticket_id, actor_user_id, field_name, old_value, new_value)
VALUES ($1, $2, $3, $4, $5, $6)
)SQL",
                       {Common::uuidV4(), ticketId, actorId, std::string(field), oldParam, newParam},
                       "Insert edit history");
        };
        recordHistory("summary", oldSummary, request.summary);
        recordHistory("description", oldDescription, request.description);
        recordHistory("priority", oldPriorityKey, request.priorityKey);
        recordHistory("assignee", historyText(oldAssigneeEmail), historyText(request.assigneeEmail));
        recordHistory("story_points", historyText(oldStoryPoints), historyText(request.storyPoints));
        recordHistory("due_date", historyText(oldDueDate), historyText(request.dueDate));
        recordHistory("ticket_type", oldTypeKey, request.ticketTypeKey);
        recordHistory("parent", historyText(oldParentKey), historyText(request.parentTicketKey));
        recordHistory("component", historyText(oldComponentName), historyText(request.componentName));

        exec(connection.get(), "COMMIT", "Commit edit ticket transaction");
        auto result = execParams(connection.get(), std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND i.id = $1",
                                 {ticketId}, "Read edited ticket");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Edited ticket could not be read back");
        }
        return readTicket(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback edit ticket transaction");
        } catch (...) {
        }
        throw;
    }
}

Domain::Ticket PostgresDatabase::reorderTicket(const std::string& ticketKey,
                                             std::optional<std::string> beforeTicketKey) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin reorder ticket transaction");
    try {
        const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
        auto projectRow = execParams(connection.get(),
                                     "SELECT project_id FROM tickets WHERE id = $1 FOR UPDATE",
                                     {ticketId},
                                     "Lock ticket for reorder");
        const std::string projectId = value(projectRow.get(), 0, 0);

        std::optional<std::string> beforeTicketId;
        if (beforeTicketKey.has_value() && !beforeTicketKey->empty()) {
            const std::string resolvedBeforeId = lookupTicketId(connection.get(), *beforeTicketKey);
            if (resolvedBeforeId == ticketId) {
                throw std::invalid_argument("Cannot reorder a ticket before itself");
            }
            auto beforeProjectRow = execParams(connection.get(),
                                               "SELECT project_id FROM tickets WHERE id = $1",
                                               {resolvedBeforeId},
                                               "Read reorder anchor project");
            if (value(beforeProjectRow.get(), 0, 0) != projectId) {
                throw std::invalid_argument("Cannot reorder relative to a ticket in a different project");
            }
            beforeTicketId = resolvedBeforeId;
        }

        // Full renumbering pass (D31): sufficient for small per-project ticket
        // counts, and simpler than a minimal-diff fractional/shift scheme.
        auto listResult = execParams(connection.get(),
                                     "SELECT id FROM tickets WHERE project_id = $1 AND deleted_at IS NULL ORDER BY rank_order, ticket_number",
                                     {projectId},
                                     "List project tickets for reorder");
        std::vector<std::string> orderedIds;
        orderedIds.reserve(static_cast<std::size_t>(PQntuples(listResult.get())));
        for (int row = 0; row < PQntuples(listResult.get()); ++row) {
            orderedIds.push_back(value(listResult.get(), row, 0));
        }

        orderedIds.erase(std::remove(orderedIds.begin(), orderedIds.end(), ticketId), orderedIds.end());
        if (beforeTicketId.has_value()) {
            const auto position = std::find(orderedIds.begin(), orderedIds.end(), *beforeTicketId);
            orderedIds.insert(position, ticketId);
        } else {
            orderedIds.push_back(ticketId);
        }

        for (std::size_t index = 0; index < orderedIds.size(); ++index) {
            const std::int64_t newRank = static_cast<std::int64_t>(index) + 1;
            execParams(connection.get(),
                       "UPDATE tickets SET rank_order = $1::bigint WHERE id = $2 AND rank_order <> $1::bigint",
                       {std::to_string(newRank), orderedIds[index]},
                       "Update ticket rank order");
        }

        exec(connection.get(), "COMMIT", "Commit reorder ticket transaction");
        auto result = execParams(connection.get(), std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND i.id = $1",
                                 {ticketId}, "Read reordered ticket");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Reordered ticket could not be read back");
        }
        return readTicket(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback reorder ticket transaction");
        } catch (...) {
        }
        throw;
    }
}

Domain::Ticket PostgresDatabase::moveTicket(const std::string& ticketKey,
                                          const std::string& targetProjectKey,
                                          const std::string& actorUserId) {
    auto connection = connect(connectionString_);
    exec(connection.get(), "BEGIN", "Begin move ticket transaction");
    try {
        const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
        auto current = execParams(connection.get(), R"SQL(
SELECT i.project_id, p.project_key, i.ticket_key, i.parent_ticket_id
FROM tickets i JOIN projects p ON p.id = i.project_id
WHERE i.id = $1
FOR UPDATE OF i
)SQL",
                                  {ticketId},
                                  "Lock ticket for move");
        const std::string currentProjectId = value(current.get(), 0, 0);
        const std::string currentProjectKey = value(current.get(), 0, 1);
        const std::string currentTicketKey = value(current.get(), 0, 2);
        const bool hasParent = PQgetisnull(current.get(), 0, 3) == 0;
        if (hasParent) {
            throw std::invalid_argument("Cannot move a ticket that has a parent");
        }

        auto childCheck = execParams(connection.get(),
                                     "SELECT COUNT(*) FROM tickets WHERE parent_ticket_id = $1 AND deleted_at IS NULL",
                                     {ticketId},
                                     "Count child tickets");
        if (int64Value(childCheck.get(), 0, 0) > 0) {
            throw std::invalid_argument("Cannot move a ticket that has child tickets");
        }

        auto project = execParams(connection.get(),
                                  "SELECT id, next_ticket_number FROM projects WHERE project_key = $1 AND archived = FALSE AND deleted_at IS NULL FOR UPDATE",
                                  {targetProjectKey},
                                  "Lock target project");
        if (PQntuples(project.get()) != 1) {
            throw std::invalid_argument("Unknown project: " + targetProjectKey);
        }
        const std::string targetProjectId = value(project.get(), 0, 0);
        if (targetProjectId == currentProjectId) {
            throw std::invalid_argument("Ticket is already in project: " + targetProjectKey);
        }
        const std::int64_t ticketNumber = int64Value(project.get(), 0, 1);
        const std::string newTicketKey = targetProjectKey + "-" + std::to_string(ticketNumber);

        execParams(connection.get(),
                   "UPDATE projects SET next_ticket_number = next_ticket_number + 1, updated_at = CURRENT_TIMESTAMP WHERE id = $1",
                   {targetProjectId},
                   "Increment target project ticket counter");

        // Append-at-end within the target project, same as createTicket (D31).
        auto maxRank = execParams(connection.get(),
                                  "SELECT COALESCE(MAX(rank_order), 0) + 1 FROM tickets WHERE project_id = $1 AND deleted_at IS NULL",
                                  {targetProjectId},
                                  "Compute next rank order for move");
        const std::int64_t rankOrder = int64Value(maxRank.get(), 0, 0);

        execParams(connection.get(), R"SQL(
UPDATE tickets
SET project_id = $1, ticket_number = $2::bigint, ticket_key = $3, rank_order = $4::bigint, updated_at = CURRENT_TIMESTAMP
WHERE id = $5
)SQL",
                   {targetProjectId, std::to_string(ticketNumber), newTicketKey, std::to_string(rankOrder), ticketId},
                   "Update ticket for move");

        // The vacated key becomes a permanent alias (D38); safe because
        // ticket_key_aliases.alias_key is a PRIMARY KEY (no collision) and
        // ticket numbers/keys are never reused.
        execParams(connection.get(),
                   "INSERT INTO ticket_key_aliases(alias_key, ticket_id) VALUES ($1, $2)",
                   {currentTicketKey, ticketId},
                   "Insert ticket key alias");

        const std::string actorId = requireUserId(connection.get(), actorUserId);
        execParams(connection.get(), R"SQL(
INSERT INTO ticket_history(id, ticket_id, actor_user_id, field_name, old_value, new_value)
VALUES ($1, $2, $3, 'project', $4, $5)
)SQL",
                   {Common::uuidV4(), ticketId, actorId, currentProjectKey, targetProjectKey},
                   "Insert move history");

        exec(connection.get(), "COMMIT", "Commit move ticket transaction");
        auto result = execParams(connection.get(), std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND i.id = $1",
                                 {ticketId}, "Read moved ticket");
        if (PQntuples(result.get()) != 1) {
            throw std::runtime_error("Moved ticket could not be read back");
        }
        return readTicket(result.get(), 0);
    } catch (...) {
        try {
            exec(connection.get(), "ROLLBACK", "Rollback move ticket transaction");
        } catch (...) {
        }
        throw;
    }
}

std::vector<Domain::TicketHistoryEntry> PostgresDatabase::listTicketHistory(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(TicketHistorySelect) + R"SQL(
JOIN tickets i ON i.id = h.ticket_id
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))
ORDER BY h.created_at DESC, h.id DESC
)SQL",
                             {ticketKey},
                             "List ticket history");
    std::vector<Domain::TicketHistoryEntry> entries;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        entries.push_back(readTicketHistoryEntry(result.get(), row));
    }
    return entries;
}

std::vector<Domain::Comment> PostgresDatabase::listComments(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(CommentSelect) + R"SQL(
JOIN tickets i ON i.id = c.ticket_id
WHERE c.deleted_at IS NULL
  AND i.deleted_at IS NULL
  AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))
ORDER BY c.created_at
)SQL",
                             {ticketKey},
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
    const std::string ticketId = lookupTicketId(connection.get(), request.ticketKey);
    const std::string authorId = requireUserId(connection.get(), authorUserId);
    const std::string commentId = Common::uuidV4();
    execParams(connection.get(), R"SQL(
INSERT INTO comments(id, ticket_id, author_user_id, body, created_at, updated_at)
VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
               {commentId, ticketId, authorId, request.body},
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
// per-edit actor column (unlike ticket_history) -- only `edited_at` is
// tracked. Kept in the signature for symmetry with editTicket and in case a
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
    notification.ticketKey = optionalValue(result, row, 2);
    notification.ticketSummary = optionalValue(result, row, 3);
    notification.readAt = optionalValue(result, row, 4);
    notification.createdAt = value(result, row, 5);
    return notification;
}

constexpr const char* NotificationSelect = R"SQL(
SELECT n.id, n.type, i.ticket_key, i.summary, n.read_at::text, n.created_at::text
FROM notifications n
LEFT JOIN tickets i ON i.id = n.ticket_id
)SQL";
} // namespace

Domain::Notification PostgresDatabase::createNotification(const std::string& userId,
                                                           const std::string& type,
                                                           const std::string& ticketId) {
    auto connection = connect(connectionString_);
    const std::string notificationId = Common::uuidV4();
    execParams(connection.get(), R"SQL(
INSERT INTO notifications(id, user_id, type, ticket_id, created_at) VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)
)SQL",
               {notificationId, userId, type, ticketId},
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

std::vector<Domain::Notification> PostgresDatabase::listNotifications(const std::string& userId, const bool unreadOnly,
                                                                        const int limit, const int offset) {
    auto connection = connect(connectionString_);
    std::string sql = std::string(NotificationSelect) + "WHERE n.user_id = $1";
    if (unreadOnly) {
        sql += " AND n.read_at IS NULL";
    }
    sql += " ORDER BY n.created_at DESC LIMIT $2::int OFFSET $3::int";
    auto result = execParams(connection.get(), sql,
                             {userId, std::to_string(limit), std::to_string(offset)},
                             "List notifications (paginated)");
    std::vector<Domain::Notification> notifications;
    const int rowCount = PQntuples(result.get());
    for (int row = 0; row < rowCount; ++row) {
        notifications.push_back(readNotification(result.get(), row));
    }
    return notifications;
}

std::int64_t PostgresDatabase::countNotifications(const std::string& userId, const bool unreadOnly) {
    auto connection = connect(connectionString_);
    std::string sql = "SELECT COUNT(*) FROM notifications WHERE user_id = $1";
    if (unreadOnly) {
        sql += " AND read_at IS NULL";
    }
    auto result = execParams(connection.get(), sql, {userId}, "Count notifications");
    if (PQntuples(result.get()) == 0) {
        return 0;
    }
    return int64Value(result.get(), 0, 0);
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

std::vector<Domain::Worklog> PostgresDatabase::listWorklogs(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), std::string(WorklogSelect) + R"SQL(
JOIN tickets i ON i.id = w.ticket_id
WHERE w.deleted_at IS NULL
  AND i.deleted_at IS NULL
  AND (i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1))
ORDER BY w.work_date DESC, w.created_at DESC
)SQL",
                             {ticketKey},
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
    const std::string ticketId = lookupTicketId(connection.get(), request.ticketKey);
    const std::string authorId = requireUserId(connection.get(), authorUserId);
    const std::string worklogId = Common::uuidV4();

    execParams(connection.get(), R"SQL(
INSERT INTO worklogs(id, ticket_id, author_user_id, work_date, time_spent_seconds, comment, created_at, updated_at)
VALUES ($1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
               {worklogId, ticketId, authorId, request.workDate, std::to_string(request.timeSpentSeconds), request.comment},
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

std::vector<Domain::AuditEvent> PostgresDatabase::listAuditEvents(const int limit, const int offset) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(),
                             std::string(AuditEventSelect) + "ORDER BY a.created_at DESC LIMIT $1::int OFFSET $2::int",
                             {std::to_string(limit), std::to_string(offset)}, "List audit events (paginated)");
    std::vector<Domain::AuditEvent> events;
    const int rowCount = PQntuples(result.get());
    for (int row = 0; row < rowCount; ++row) {
        events.push_back(readAuditEvent(result.get(), row));
    }
    return events;
}

std::int64_t PostgresDatabase::countAuditEvents() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), "SELECT COUNT(*) FROM audit_events", "Count audit events");
    if (PQntuples(result.get()) == 0) {
        return 0;
    }
    return int64Value(result.get(), 0, 0);
}

Domain::DashboardStats PostgresDatabase::dashboardStats() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), R"SQL(
SELECT COUNT(i.id),
       COUNT(i.id) FILTER (WHERE s.category = 'todo'),
       COUNT(i.id) FILTER (WHERE s.category = 'in_progress'),
       COUNT(i.id) FILTER (WHERE s.category = 'done')
FROM tickets i JOIN ticket_statuses s ON s.id = i.status_id
WHERE i.deleted_at IS NULL
)SQL",
                       "Dashboard statistics");
    Domain::DashboardStats stats;
    stats.totalTickets = int64Value(result.get(), 0, 0);
    stats.todoTickets = int64Value(result.get(), 0, 1);
    stats.inProgressTickets = int64Value(result.get(), 0, 2);
    stats.doneTickets = int64Value(result.get(), 0, 3);
    auto recent = listTickets({});
    if (recent.size() > 8) {
        recent.resize(8);
    }
    stats.recentTickets = std::move(recent);
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
WHERE status_id = (SELECT id FROM ticket_statuses WHERE status_key = $2)
)SQL",
                             {wipLimit ? std::optional<std::string>(std::to_string(*wipLimit)) : std::nullopt, statusKey},
                             "Set board column WIP limit");
    return std::string(PQcmdTuples(result.get())) != "0";
}

Domain::TicketLink PostgresDatabase::createTicketLink(const std::string& sourceTicketKey,
                                                    const std::string& targetTicketKey,
                                                    const std::string& linkType) {
    auto connection = connect(connectionString_);
    const std::string sourceId = lookupTicketId(connection.get(), sourceTicketKey);
    const std::string targetId = lookupTicketId(connection.get(), targetTicketKey);

    auto duplicate = execParams(connection.get(),
        "SELECT 1 FROM ticket_links WHERE source_ticket_id = $1 AND target_ticket_id = $2 AND link_type = $3",
        {sourceId, targetId, linkType},
        "Check duplicate ticket link");
    if (PQntuples(duplicate.get()) != 0) {
        throw std::invalid_argument("That link already exists");
    }

    const std::string linkId = Common::uuidV4();
    execParams(connection.get(),
               "INSERT INTO ticket_links(id, source_ticket_id, target_ticket_id, link_type) VALUES ($1, $2, $3, $4)",
               {linkId, sourceId, targetId, linkType},
               "Insert ticket link");

    Domain::TicketLink link;
    link.id = linkId;
    link.linkType = linkType;
    link.outward = true;

    auto targetRow = execParams(connection.get(), "SELECT ticket_key, summary FROM tickets WHERE id = $1",
                                {targetId}, "Read linked ticket");
    if (PQntuples(targetRow.get()) == 1) {
        link.otherTicketKey = value(targetRow.get(), 0, 0);
        link.otherTicketSummary = value(targetRow.get(), 0, 1);
    }
    return link;
}

std::vector<Domain::TicketLink> PostgresDatabase::listTicketLinks(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);

    auto result = execParams(connection.get(), R"SQL(
SELECT l.id, l.link_type, TRUE AS outward, tgt.ticket_key, tgt.summary
FROM ticket_links l JOIN tickets tgt ON tgt.id = l.target_ticket_id
WHERE l.source_ticket_id = $1 AND tgt.deleted_at IS NULL
UNION ALL
SELECT l.id, l.link_type, FALSE AS outward, src.ticket_key, src.summary
FROM ticket_links l JOIN tickets src ON src.id = l.source_ticket_id
WHERE l.target_ticket_id = $1 AND src.deleted_at IS NULL
)SQL",
                             {ticketId},
                             "List ticket links");
    std::vector<Domain::TicketLink> links;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        Domain::TicketLink link;
        link.id = value(result.get(), row, 0);
        link.linkType = value(result.get(), row, 1);
        link.outward = boolValue(result.get(), row, 2);
        link.otherTicketKey = value(result.get(), row, 3);
        link.otherTicketSummary = value(result.get(), row, 4);
        links.push_back(std::move(link));
    }
    return links;
}

std::optional<Domain::TicketLinkDetail> PostgresDatabase::findTicketLinkById(const std::string& linkId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT l.id, l.link_type, src.ticket_key, srcProj.project_key, tgt.ticket_key, tgtProj.project_key
FROM ticket_links l
JOIN tickets src ON src.id = l.source_ticket_id
JOIN projects srcProj ON srcProj.id = src.project_id
JOIN tickets tgt ON tgt.id = l.target_ticket_id
JOIN projects tgtProj ON tgtProj.id = tgt.project_id
WHERE l.id = $1
)SQL",
                             {linkId},
                             "Find ticket link");
    if (PQntuples(result.get()) != 1) {
        return std::nullopt;
    }
    Domain::TicketLinkDetail detail;
    detail.id = value(result.get(), 0, 0);
    detail.linkType = value(result.get(), 0, 1);
    detail.sourceTicketKey = value(result.get(), 0, 2);
    detail.sourceProjectKey = value(result.get(), 0, 3);
    detail.targetTicketKey = value(result.get(), 0, 4);
    detail.targetProjectKey = value(result.get(), 0, 5);
    return detail;
}

bool PostgresDatabase::deleteTicketLink(const std::string& linkId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "DELETE FROM ticket_links WHERE id = $1", {linkId}, "Delete ticket link");
    return std::string(PQcmdTuples(result.get())) != "0";
}

namespace {
// Shared by the two structurally-identical watch/vote tables (ticket_watchers,
// ticket_votes: both (ticket_id, user_id) composite-PK many-to-many tables with
// no other columns worth reading back). `table` is always a fixed internal
// literal, never caller input, matching the existing `lookupId` pattern.
bool insertMembership(PGconn* connection, const char* table, const std::string& ticketId, const std::string& userId) {
    auto result = execParams(connection,
                             std::string("INSERT INTO ") + table + "(ticket_id, user_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
                             {ticketId, userId},
                             std::string("Insert into ") + table);
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool deleteMembership(PGconn* connection, const char* table, const std::string& ticketId, const std::string& userId) {
    auto result = execParams(connection,
                             std::string("DELETE FROM ") + table + " WHERE ticket_id = $1 AND user_id = $2",
                             {ticketId, userId},
                             std::string("Delete from ") + table);
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::UserSummary> listMembers(PGconn* connection, const char* table, const std::string& ticketId) {
    auto result = execParams(connection,
                             std::string("SELECT u.id, u.display_name, u.email FROM ") + table
                                 + " t JOIN users u ON u.id = t.user_id WHERE t.ticket_id = $1 ORDER BY u.display_name",
                             {ticketId},
                             std::string("List ") + table);
    std::vector<Domain::UserSummary> users;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        users.push_back(readUserSummary(result.get(), row, 0));
    }
    return users;
}
} // namespace

bool PostgresDatabase::watchTicket(const std::string& ticketKey, const std::string& userId) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
    const std::string resolvedUserId = requireUserId(connection.get(), userId);
    return insertMembership(connection.get(), "ticket_watchers", ticketId, resolvedUserId);
}

bool PostgresDatabase::unwatchTicket(const std::string& ticketKey, const std::string& userId) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
    return deleteMembership(connection.get(), "ticket_watchers", ticketId, userId);
}

std::vector<Domain::UserSummary> PostgresDatabase::listWatchers(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
    return listMembers(connection.get(), "ticket_watchers", ticketId);
}

std::vector<Domain::Ticket> PostgresDatabase::listWatchedTickets(const std::string& userId, int limit) {
    auto connection = connect(connectionString_);
    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND EXISTS (SELECT 1 FROM ticket_watchers w WHERE w.ticket_id = i.id AND w.user_id = $1)
ORDER BY i.updated_at DESC
LIMIT $2
)SQL";
    auto result = execParams(connection.get(), sql, {userId, std::to_string(limit)}, "List watched tickets");
    std::vector<Domain::Ticket> tickets;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        tickets.push_back(readTicket(result.get(), row));
    }
    return tickets;
}

bool PostgresDatabase::voteTicket(const std::string& ticketKey, const std::string& userId) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
    const std::string resolvedUserId = requireUserId(connection.get(), userId);
    return insertMembership(connection.get(), "ticket_votes", ticketId, resolvedUserId);
}

bool PostgresDatabase::unvoteTicket(const std::string& ticketKey, const std::string& userId) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
    return deleteMembership(connection.get(), "ticket_votes", ticketId, userId);
}

std::vector<Domain::UserSummary> PostgresDatabase::listVoters(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
    return listMembers(connection.get(), "ticket_votes", ticketId);
}

bool PostgresDatabase::softDeleteTicket(const std::string& ticketKey, const std::string& actorUserId) {
    auto connection = connect(connectionString_);
    const std::string actorId = requireUserId(connection.get(), actorUserId);
    auto result = execParams(connection.get(), R"SQL(
UPDATE tickets SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = $1, updated_at = CURRENT_TIMESTAMP
WHERE (ticket_key = $2 OR id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $2)) AND deleted_at IS NULL
)SQL",
                             {actorId, ticketKey},
                             "Soft delete ticket");
    return std::string(PQcmdTuples(result.get())) != "0";
}

bool PostgresDatabase::restoreTicket(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
UPDATE tickets SET deleted_at = NULL, deleted_by_user_id = NULL, updated_at = CURRENT_TIMESTAMP
WHERE ticket_key = $1 AND deleted_at IS NOT NULL
)SQL",
                             {ticketKey},
                             "Restore ticket");
    return std::string(PQcmdTuples(result.get())) != "0";
}

std::vector<Domain::Ticket> PostgresDatabase::listDeletedTickets() {
    auto connection = connect(connectionString_);
    exec(connection.get(), "DELETE FROM tickets WHERE deleted_at IS NOT NULL AND deleted_at <= CURRENT_TIMESTAMP - INTERVAL '90 days'",
        "Purge expired deleted tickets");

    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NOT NULL
ORDER BY i.deleted_at DESC
)SQL";
    auto result = exec(connection.get(), sql, "List deleted tickets");
    std::vector<Domain::Ticket> tickets;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        tickets.push_back(readTicket(result.get(), row));
    }
    return tickets;
}

bool PostgresDatabase::permanentlyDeleteTicket(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "DELETE FROM tickets WHERE ticket_key = $1 AND deleted_at IS NOT NULL",
                             {ticketKey}, "Permanently delete ticket");
    return std::string(PQcmdTuples(result.get())) != "0";
}

Domain::Attachment PostgresDatabase::createAttachment(const std::string& id,
                                                      const std::string& ticketKey,
                                                      const std::string& uploaderUserId,
                                                      const std::string& fileName,
                                                      const std::string& contentType,
                                                      const std::int64_t byteSize,
                                                      const std::string& sha256) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
    const std::string uploaderId = requireUserId(connection.get(), uploaderUserId);
    execParams(connection.get(), R"SQL(
INSERT INTO attachments(id, ticket_id, uploader_user_id, file_name, content_type, byte_size, storage_key, sha256)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
)SQL",
              {id, ticketId, uploaderId, fileName, contentType, std::to_string(byteSize), id, sha256},
              "Create attachment");

    auto result = execParams(connection.get(), std::string(AttachmentSelect) + "WHERE a.id = $1", {id}, "Read created attachment");
    if (PQntuples(result.get()) == 0) {
        throw std::runtime_error("Failed to read back created attachment");
    }
    return readAttachment(result.get(), 0);
}

std::vector<Domain::Attachment> PostgresDatabase::listAttachments(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    const std::string ticketId = lookupTicketId(connection.get(), ticketKey);
    auto result = execParams(connection.get(),
                             std::string(AttachmentSelect) + "WHERE a.ticket_id = $1 AND a.deleted_at IS NULL ORDER BY a.created_at",
                             {ticketId}, "List attachments");
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

std::vector<std::string> PostgresDatabase::listAttachmentStorageKeysForTicket(const std::string& ticketKey) {
    auto connection = connect(connectionString_);
    // Deliberately does not use lookupTicketId (which excludes soft-deleted
    // tickets): this is called right before a permanent delete, at which
    // point the ticket is expected to already be soft-deleted.
    auto result = execParams(connection.get(), R"SQL(
SELECT a.storage_key FROM attachments a JOIN tickets i ON i.id = a.ticket_id
WHERE i.ticket_key = $1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = $1)
)SQL",
                             {ticketKey}, "List attachment storage keys for ticket");
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
JOIN tickets i ON i.id = a.ticket_id
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

namespace {
std::string joinComma(const std::vector<std::string>& values) {
    std::string joined;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            joined += ",";
        }
        joined += values[i];
    }
    return joined;
}

constexpr const char* WebhookSubscriptionSelect = R"SQL(
SELECT w.id, w.target_url, w.secret, w.event_types, w.project_key, w.enabled,
       u.id, u.display_name, u.email, w.created_at::text
FROM webhook_subscriptions w
LEFT JOIN users u ON u.id = w.created_by_user_id
)SQL";

Domain::WebhookSubscription readWebhookSubscription(PGresult* result, int row) {
    Domain::WebhookSubscription subscription;
    subscription.id = value(result, row, 0);
    subscription.targetUrl = value(result, row, 1);
    subscription.secret = value(result, row, 2);
    subscription.eventTypes = splitLabels(value(result, row, 3));
    subscription.projectKey = optionalValue(result, row, 4);
    subscription.enabled = boolValue(result, row, 5);
    if (PQgetisnull(result, row, 6) == 0) {
        subscription.createdBy = readUserSummary(result, row, 6);
    }
    subscription.createdAt = value(result, row, 9);
    return subscription;
}
} // namespace

std::vector<Domain::WebhookSubscription> PostgresDatabase::listWebhookSubscriptions() {
    auto connection = connect(connectionString_);
    auto result = exec(connection.get(), std::string(WebhookSubscriptionSelect) + "ORDER BY w.created_at DESC",
                       "List webhook subscriptions");
    std::vector<Domain::WebhookSubscription> subscriptions;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        subscriptions.push_back(readWebhookSubscription(result.get(), row));
    }
    return subscriptions;
}

Domain::WebhookSubscription PostgresDatabase::createWebhookSubscription(
    const Domain::CreateWebhookSubscriptionRequest& request, const std::string& createdByUserId) {
    auto connection = connect(connectionString_);
    const std::string subscriptionId = Common::uuidV4();
    // See SqliteDatabase::createWebhookSubscription for why the secret is
    // generated here and stored in cleartext rather than hashed.
    const std::string secret = Common::randomTokenHex(32);
    execParams(connection.get(), R"SQL(
INSERT INTO webhook_subscriptions(id, target_url, secret, event_types, project_key, created_by_user_id, created_at)
VALUES ($1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP)
)SQL",
               {subscriptionId, request.targetUrl, secret, joinComma(request.eventTypes), request.projectKey,
                createdByUserId},
               "Insert webhook subscription");

    auto result = execParams(connection.get(), std::string(WebhookSubscriptionSelect) + "WHERE w.id = $1",
                             {subscriptionId}, "Read created webhook subscription");
    if (PQntuples(result.get()) != 1) {
        throw std::runtime_error("Created webhook subscription could not be read back");
    }
    return readWebhookSubscription(result.get(), 0);
}

bool PostgresDatabase::deleteWebhookSubscription(const std::string& subscriptionId) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), "DELETE FROM webhook_subscriptions WHERE id = $1",
                             {subscriptionId}, "Delete webhook subscription");
    return std::string(PQcmdTuples(result.get())) != "0";
}

void PostgresDatabase::createWebhookDelivery(const std::string& subscriptionId, const std::string& eventType,
                                              const std::string& payload) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), R"SQL(
INSERT INTO webhook_deliveries(id, subscription_id, event_type, payload, created_at, next_attempt_at)
VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
               {Common::uuidV4(), subscriptionId, eventType, payload}, "Insert webhook delivery");
}

std::vector<Domain::WebhookDelivery> PostgresDatabase::listPendingWebhookDeliveries(const int limit) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT d.id, d.subscription_id, w.target_url, w.secret, d.event_type, d.payload, d.attempt_count
FROM webhook_deliveries d
JOIN webhook_subscriptions w ON w.id = d.subscription_id
WHERE d.status = 'pending' AND d.next_attempt_at <= CURRENT_TIMESTAMP
ORDER BY d.created_at
LIMIT $1::int
)SQL",
                             {std::to_string(limit)}, "List pending webhook deliveries");
    std::vector<Domain::WebhookDelivery> deliveries;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        Domain::WebhookDelivery delivery;
        delivery.id = value(result.get(), row, 0);
        delivery.subscriptionId = value(result.get(), row, 1);
        delivery.targetUrl = value(result.get(), row, 2);
        delivery.secret = value(result.get(), row, 3);
        delivery.eventType = value(result.get(), row, 4);
        delivery.payload = value(result.get(), row, 5);
        delivery.attemptCount = static_cast<int>(int64Value(result.get(), row, 6));
        deliveries.push_back(std::move(delivery));
    }
    return deliveries;
}

void PostgresDatabase::recordWebhookDeliveryResult(const std::string& deliveryId, const bool success,
                                                    const std::optional<std::string>& error) {
    auto connection = connect(connectionString_);
    if (success) {
        execParams(connection.get(), R"SQL(
UPDATE webhook_deliveries SET status = 'delivered', delivered_at = CURRENT_TIMESTAMP, last_error = NULL
WHERE id = $1
)SQL",
                   {deliveryId}, "Record webhook delivery success");
        return;
    }
    // See SqliteDatabase::recordWebhookDeliveryResult -- the CASE/increment
    // both read the row's pre-update attempt_count, so this stays a single
    // consistent statement.
    execParams(connection.get(), R"SQL(
UPDATE webhook_deliveries
SET attempt_count = attempt_count + 1,
    last_error = $2,
    status = CASE WHEN attempt_count + 1 >= $3::int THEN 'failed' ELSE status END,
    next_attempt_at = CURRENT_TIMESTAMP + ($4::int || ' minutes')::interval
WHERE id = $1
)SQL",
               {deliveryId, error, std::to_string(Domain::MaxDeliveryAttempts),
                std::to_string(Domain::DeliveryRetryDelayMinutes)},
               "Record webhook delivery failure");
}

void PostgresDatabase::createEmailDelivery(const std::string& recipientUserId, const std::string& subject,
                                            const std::string& body) {
    auto connection = connect(connectionString_);
    execParams(connection.get(), R"SQL(
INSERT INTO email_deliveries(id, recipient_user_id, subject, body, created_at, next_attempt_at)
VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL",
               {Common::uuidV4(), recipientUserId, subject, body}, "Insert email delivery");
}

std::vector<Domain::EmailDelivery> PostgresDatabase::listPendingEmailDeliveries(const int limit) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT d.id, u.email, d.subject, d.body, d.attempt_count
FROM email_deliveries d
JOIN users u ON u.id = d.recipient_user_id
WHERE d.status = 'pending' AND d.next_attempt_at <= CURRENT_TIMESTAMP
ORDER BY d.created_at
LIMIT $1::int
)SQL",
                             {std::to_string(limit)}, "List pending email deliveries");
    std::vector<Domain::EmailDelivery> deliveries;
    for (int row = 0; row < PQntuples(result.get()); ++row) {
        Domain::EmailDelivery delivery;
        delivery.id = value(result.get(), row, 0);
        delivery.recipientEmail = value(result.get(), row, 1);
        delivery.subject = value(result.get(), row, 2);
        delivery.body = value(result.get(), row, 3);
        delivery.attemptCount = static_cast<int>(int64Value(result.get(), row, 4));
        deliveries.push_back(std::move(delivery));
    }
    return deliveries;
}

void PostgresDatabase::recordEmailDeliveryResult(const std::string& deliveryId, const bool success,
                                                  const std::optional<std::string>& error) {
    auto connection = connect(connectionString_);
    if (success) {
        execParams(connection.get(), R"SQL(
UPDATE email_deliveries SET status = 'delivered', sent_at = CURRENT_TIMESTAMP, last_error = NULL WHERE id = $1
)SQL",
                   {deliveryId}, "Record email delivery success");
        return;
    }
    execParams(connection.get(), R"SQL(
UPDATE email_deliveries
SET attempt_count = attempt_count + 1,
    last_error = $2,
    status = CASE WHEN attempt_count + 1 >= $3::int THEN 'failed' ELSE status END,
    next_attempt_at = CURRENT_TIMESTAMP + ($4::int || ' minutes')::interval
WHERE id = $1
)SQL",
               {deliveryId, error, std::to_string(Domain::MaxDeliveryAttempts),
                std::to_string(Domain::DeliveryRetryDelayMinutes)},
               "Record email delivery failure");
}

std::optional<Domain::IdempotencyRecord> PostgresDatabase::findIdempotencyRecord(
    const std::string& userId, const std::string& idempotencyKey) {
    auto connection = connect(connectionString_);
    auto result = execParams(connection.get(), R"SQL(
SELECT request_hash, response_status, response_body FROM idempotency_keys
WHERE user_id = $1 AND idempotency_key = $2
)SQL",
                             {userId, idempotencyKey}, "Find idempotency record");
    if (PQntuples(result.get()) != 1) {
        return std::nullopt;
    }
    Domain::IdempotencyRecord record;
    record.requestHash = value(result.get(), 0, 0);
    record.responseStatus = static_cast<int>(int64Value(result.get(), 0, 1));
    record.responseBody = value(result.get(), 0, 2);
    return record;
}

void PostgresDatabase::recordIdempotencyResult(const std::string& userId, const std::string& idempotencyKey,
                                               const std::string& requestHash, const int responseStatus,
                                               const std::string& responseBody) {
    auto connection = connect(connectionString_);
    // Best-effort: ON CONFLICT DO NOTHING rather than a plain INSERT, so a
    // narrow concurrent-retry race (two requests carrying the same key
    // arriving before either has stored its result) never surfaces as a
    // 500 -- the caller's own response has already been computed and
    // returned either way, this write is only about caching it for a
    // *future* retry.
    execParams(connection.get(), R"SQL(
INSERT INTO idempotency_keys(user_id, idempotency_key, request_hash, response_status, response_body, created_at)
VALUES ($1, $2, $3, $4::int, $5, CURRENT_TIMESTAMP)
ON CONFLICT (user_id, idempotency_key) DO NOTHING
)SQL",
               {userId, idempotencyKey, requestHash, std::to_string(responseStatus), responseBody},
               "Record idempotency result");
}

} // namespace TicketHub::Infrastructure::Database
