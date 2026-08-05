#include "infrastructure/database/SqliteDatabase.h"

#include "common/FileUtil.h"
#include "common/RandomToken.h"
#include "common/Uuid.h"
#include "infrastructure/database/Migration.h"
#include "domain/Errors.h"

#include <algorithm>
#include <filesystem>
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

bool boolColumn(sqlite3_stmt* statement, int column) {
    return sqlite3_column_int(statement, column) != 0;
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

Domain::UserSummary readUserSummary(sqlite3_stmt* statement, int offset) {
    return Domain::UserSummary{text(statement, offset), text(statement, offset + 1), text(statement, offset + 2)};
}

Domain::User readUser(sqlite3_stmt* statement) {
    Domain::User user;
    user.id = text(statement, 0);
    user.email = text(statement, 1);
    user.displayName = text(statement, 2);
    user.handle = optionalText(statement, 3);
    user.timeZone = text(statement, 4);
    user.clockFormat = text(statement, 5);
    user.active = boolColumn(statement, 6);
    user.isAdmin = boolColumn(statement, 7);
    user.createdAt = text(statement, 8);
    return user;
}

constexpr const char* UserSelect =
    "SELECT id, email, display_name, handle, time_zone, clock_format, active, is_admin, created_at FROM users";

// Project components (D19). `lead`/`defaultAssignee` reuse readUserSummary
// like Project.lead does; the compact ComponentSummary shape embedded in a
// Ticket is filled separately, straight off TicketSelect's own comp.id/
// comp.name columns (see readTicket), not via this full row.
constexpr const char* ComponentSelect = R"SQL(
SELECT c.id, p.project_key, c.name, c.description,
       lead.id, lead.display_name, lead.email,
       def.id, def.display_name, def.email,
       c.created_at, c.updated_at
FROM project_components c
JOIN projects p ON p.id = c.project_id
LEFT JOIN users lead ON lead.id = c.lead_user_id
LEFT JOIN users def ON def.id = c.default_assignee_user_id
)SQL";

Domain::ProjectComponent readComponent(sqlite3_stmt* statement) {
    Domain::ProjectComponent component;
    component.id = text(statement, 0);
    component.projectKey = text(statement, 1);
    component.name = text(statement, 2);
    component.description = text(statement, 3);
    if (sqlite3_column_type(statement, 4) != SQLITE_NULL) {
        component.lead = readUserSummary(statement, 4);
    }
    if (sqlite3_column_type(statement, 7) != SQLITE_NULL) {
        component.defaultAssignee = readUserSummary(statement, 7);
    }
    component.createdAt = text(statement, 10);
    component.updatedAt = text(statement, 11);
    return component;
}

// Custom fields (D9). `options` is stored as a small self-contained JSON
// array of strings -- a private encoding local to this adapter (not a
// dependency on the web layer's crow::json), since the only structure ever
// needed is "flat array of strings produced and consumed by this file."
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
SELECT f.id, p.project_key, f.name, f.field_type, f.options, f.required, f.sort_order, f.created_at
FROM custom_fields f
JOIN projects p ON p.id = f.project_id
)SQL";

Domain::CustomFieldDefinition readCustomFieldDefinition(sqlite3_stmt* statement) {
    Domain::CustomFieldDefinition field;
    field.id = text(statement, 0);
    field.projectKey = text(statement, 1);
    field.name = text(statement, 2);
    field.fieldType = text(statement, 3);
    field.options = decodeCustomFieldOptions(text(statement, 4));
    field.required = boolColumn(statement, 5);
    field.sortOrder = sqlite3_column_int(statement, 6);
    field.createdAt = text(statement, 7);
    return field;
}

Domain::Ticket readTicket(sqlite3_stmt* statement) {
    Domain::Ticket ticket;
    ticket.id = text(statement, 0);
    ticket.key = text(statement, 1);
    ticket.number = sqlite3_column_int64(statement, 2);
    ticket.projectKey = text(statement, 3);
    ticket.projectName = text(statement, 4);
    ticket.summary = text(statement, 5);
    ticket.description = text(statement, 6);
    ticket.type = {text(statement, 7), text(statement, 8), text(statement, 9), text(statement, 10)};
    ticket.status = {text(statement, 11), text(statement, 12), text(statement, 13), sqlite3_column_int(statement, 14)};
    ticket.priority = {text(statement, 15), text(statement, 16), sqlite3_column_int(statement, 17), text(statement, 18)};
    ticket.reporter = readUserSummary(statement, 19);
    if (sqlite3_column_type(statement, 22) != SQLITE_NULL) {
        ticket.assignee = readUserSummary(statement, 22);
    }
    ticket.parentTicketKey = optionalText(statement, 25);
    if (sqlite3_column_type(statement, 26) != SQLITE_NULL) {
        ticket.storyPoints = sqlite3_column_double(statement, 26);
    }
    ticket.dueDate = optionalText(statement, 27);
    ticket.labels = splitLabels(text(statement, 28));
    ticket.createdAt = text(statement, 29);
    ticket.updatedAt = text(statement, 30);
    ticket.version = sqlite3_column_int64(statement, 31);
    ticket.resolution = optionalText(statement, 32);
    ticket.rankOrder = sqlite3_column_int64(statement, 33);
    if (sqlite3_column_type(statement, 34) != SQLITE_NULL) {
        ticket.component = Domain::ComponentSummary{text(statement, 34), text(statement, 35)};
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
    i.story_points, i.due_date,
    COALESCE(GROUP_CONCAT(DISTINCT l.name), ''),
    i.created_at, i.updated_at, i.version, i.resolution, i.rank_order,
    comp.id, comp.name
FROM tickets i
JOIN projects p ON p.id = i.project_id
JOIN ticket_types it ON it.id = i.ticket_type_id
JOIN ticket_statuses s ON s.id = i.status_id
JOIN priorities pr ON pr.id = i.priority_id
JOIN users reporter ON reporter.id = i.reporter_user_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
LEFT JOIN tickets parent ON parent.id = i.parent_ticket_id
LEFT JOIN ticket_labels il ON il.ticket_id = i.id
LEFT JOIN labels l ON l.id = il.label_id
LEFT JOIN project_components comp ON comp.id = i.component_id
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

std::string requireUserId(sqlite3* database, const std::string& userId) {
    return lookupId(database, "users", "id", userId);
}

// Components are named uniquely per project, not globally (D19), so the
// lookup must be scoped by projectId -- unlike lookupId's plain global
// key/value lookup used for ticket types/statuses/priorities.
std::string lookupComponentId(sqlite3* database, const std::string& projectId, const std::string& name) {
    Statement statement(database, "SELECT id FROM project_components WHERE project_id = ? AND name = ?");
    statement.bind(1, projectId);
    statement.bind(2, name);
    if (statement.step() != SQLITE_ROW) {
        throw std::invalid_argument("Unknown component: " + name);
    }
    return text(statement.get(), 0);
}

std::string lookupTicketId(sqlite3* database, const std::string& ticketKey) {
    Statement statement(database, R"SQL(
SELECT i.id
FROM tickets i
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1))
)SQL");
    statement.bind(1, ticketKey);
    if (statement.step() != SQLITE_ROW) {
        throw std::invalid_argument("Unknown ticket key: " + ticketKey);
    }
    return text(statement.get(), 0);
}

void expectDone(sqlite3* database, Statement& statement, const std::string& action) {
    if (statement.step() != SQLITE_DONE) {
        throw std::runtime_error(action + " failed: " + sqlite3_errmsg(database));
    }
}

Domain::Comment readComment(sqlite3_stmt* statement) {
    Domain::Comment comment;
    comment.id = text(statement, 0);
    comment.ticketId = text(statement, 1);
    comment.author = readUserSummary(statement, 2);
    comment.body = text(statement, 5);
    comment.createdAt = text(statement, 6);
    comment.updatedAt = text(statement, 7);
    comment.version = sqlite3_column_int64(statement, 8);
    comment.editedAt = optionalText(statement, 9);
    return comment;
}

constexpr const char* CommentSelect = R"SQL(
SELECT c.id, c.ticket_id, u.id, u.display_name, u.email,
       c.body, c.created_at, c.updated_at, c.version, c.edited_at
FROM comments c JOIN users u ON u.id = c.author_user_id
)SQL";

Domain::TicketHistoryEntry readTicketHistoryEntry(sqlite3_stmt* statement) {
    Domain::TicketHistoryEntry entry;
    entry.id = text(statement, 0);
    entry.ticketId = text(statement, 1);
    if (sqlite3_column_type(statement, 2) != SQLITE_NULL) {
        entry.actor = readUserSummary(statement, 2);
    }
    entry.fieldName = text(statement, 5);
    entry.oldValue = optionalText(statement, 6);
    entry.newValue = optionalText(statement, 7);
    entry.createdAt = text(statement, 8);
    return entry;
}

// LEFT JOIN (not JOIN, unlike CommentSelect/WorklogSelect above) --
// actor_user_id is nullable (ON DELETE SET NULL), so a since-deleted
// user's past history rows must still resolve instead of vanishing from
// the JOIN entirely.
constexpr const char* TicketHistorySelect = R"SQL(
SELECT h.id, h.ticket_id, u.id, u.display_name, u.email,
       h.field_name, h.old_value, h.new_value, h.created_at
FROM ticket_history h LEFT JOIN users u ON u.id = h.actor_user_id
)SQL";

Domain::Worklog readWorklog(sqlite3_stmt* statement) {
    Domain::Worklog worklog;
    worklog.id = text(statement, 0);
    worklog.ticketId = text(statement, 1);
    worklog.author = readUserSummary(statement, 2);
    worklog.workDate = text(statement, 5);
    worklog.timeSpentSeconds = sqlite3_column_int64(statement, 6);
    worklog.comment = optionalText(statement, 7);
    worklog.createdAt = text(statement, 8);
    worklog.updatedAt = text(statement, 9);
    worklog.version = sqlite3_column_int64(statement, 10);
    return worklog;
}

constexpr const char* WorklogSelect = R"SQL(
SELECT w.id, w.ticket_id, u.id, u.display_name, u.email,
       w.work_date, w.time_spent_seconds, w.comment, w.created_at, w.updated_at, w.version
FROM worklogs w JOIN users u ON u.id = w.author_user_id
)SQL";

Domain::Attachment readAttachment(sqlite3_stmt* statement) {
    Domain::Attachment attachment;
    attachment.id = text(statement, 0);
    attachment.ticketId = text(statement, 1);
    attachment.uploader = readUserSummary(statement, 2);
    attachment.fileName = text(statement, 5);
    attachment.contentType = text(statement, 6);
    attachment.byteSize = sqlite3_column_int64(statement, 7);
    attachment.sha256 = text(statement, 8);
    attachment.createdAt = text(statement, 9);
    attachment.deletedAt = optionalText(statement, 10);
    attachment.ticketKey = text(statement, 11);
    return attachment;
}

constexpr const char* AttachmentSelect = R"SQL(
SELECT a.id, a.ticket_id, u.id, u.display_name, u.email,
       a.file_name, a.content_type, a.byte_size, a.sha256, a.created_at, a.deleted_at, i.ticket_key
FROM attachments a JOIN users u ON u.id = a.uploader_user_id JOIN tickets i ON i.id = a.ticket_id
)SQL";

Domain::AuditEvent readAuditEvent(sqlite3_stmt* statement) {
    Domain::AuditEvent event;
    event.id = text(statement, 0);
    event.category = text(statement, 1);
    event.action = text(statement, 2);
    if (sqlite3_column_type(statement, 3) != SQLITE_NULL) {
        event.actor = readUserSummary(statement, 3);
    }
    event.targetType = optionalText(statement, 6);
    event.targetId = optionalText(statement, 7);
    event.details = optionalText(statement, 8);
    event.createdAt = text(statement, 9);
    return event;
}

// LEFT JOIN, not JOIN: actor_user_id may be NULL (a failed/blocked login
// attempt, or CLI-driven account creation, has no authenticated actor).
constexpr const char* AuditEventSelect = R"SQL(
SELECT a.id, a.category, a.action, u.id, u.display_name, u.email,
       a.target_type, a.target_id, a.details, a.created_at
FROM audit_events a LEFT JOIN users u ON u.id = a.actor_user_id
)SQL";

Domain::BoardColumn readBoardColumn(sqlite3_stmt* statement) {
    Domain::BoardColumn column;
    column.id = text(statement, 0);
    column.statusKey = text(statement, 1);
    column.statusName = text(statement, 2);
    column.sortOrder = sqlite3_column_int(statement, 3);
    if (sqlite3_column_type(statement, 4) != SQLITE_NULL) {
        column.wipLimit = sqlite3_column_int(statement, 4);
    }
    return column;
}

constexpr const char* BoardColumnSelect = R"SQL(
SELECT bc.id, s.status_key, s.name, bc.sort_order, bc.wip_limit
FROM board_columns bc JOIN ticket_statuses s ON s.id = bc.status_id
ORDER BY bc.sort_order
)SQL";

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

        // Foreign keys are disabled for the duration of each migration and
        // re-enabled (with an explicit integrity check) immediately after.
        // This is SQLite's own documented pattern for schema changes that
        // require a table rebuild (e.g. dropping a column that participates
        // in a UNIQUE constraint, which ALTER TABLE DROP COLUMN refuses to
        // do directly) -- see migrations/sqlite/004_identity.sql. The
        // PRAGMA is a no-op inside a transaction, so it must be toggled
        // outside the BEGIN/COMMIT pair.
        executeScript("PRAGMA foreign_keys = OFF;");
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
            executeScript("PRAGMA foreign_keys = ON;");
            throw;
        }
        executeScript("PRAGMA foreign_keys = ON;");
        {
            Statement check(database_, "PRAGMA foreign_key_check");
            if (check.step() == SQLITE_ROW) {
                throw std::runtime_error("Migration " + migration.version + " left dangling foreign keys");
            }
        }
    }
}

// Uses SQLite's online backup API (sqlite3_backup_*) rather than a raw file
// copy: the database is opened in WAL mode, so a plain `cp` of just the main
// file could miss data still sitting in an unmerged `-wal` file. The backup
// API produces a correct, complete snapshot regardless of WAL/checkpoint
// state -- documented as offline/maintenance-window use only (D107) to
// match what the CLI's `backup`/`restore` commands promise, not because the
// mechanism itself requires it.
void SqliteDatabase::backup(const std::string& directory) {
    std::scoped_lock lock(mutex_);
    std::filesystem::create_directories(directory);
    const auto outputPath = (std::filesystem::path(directory) / "database.sqlite3").string();
    sqlite3* destination = nullptr;
    if (sqlite3_open_v2(outputPath.c_str(), &destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const std::string message = destination == nullptr ? "unknown error" : sqlite3_errmsg(destination);
        if (destination != nullptr) {
            sqlite3_close(destination);
        }
        throw std::runtime_error("Cannot create backup output file: " + message);
    }
    sqlite3_backup* backup = sqlite3_backup_init(destination, "main", database_, "main");
    if (backup == nullptr) {
        const std::string message = sqlite3_errmsg(destination);
        sqlite3_close(destination);
        throw std::runtime_error("Cannot start SQLite backup: " + message);
    }
    const int result = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    if (result != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(destination);
        sqlite3_close(destination);
        throw std::runtime_error("SQLite backup did not complete: " + message);
    }
    sqlite3_close(destination);
}

// Same backup API used in reverse: `source` (the backup file, read-only) is
// copied into `database_` (this connection's already-open database),
// replacing its content in place. D108: direct restore into the target
// database, no isolated staging environment.
void SqliteDatabase::restore(const std::string& directory) {
    std::scoped_lock lock(mutex_);
    const auto inputPath = std::filesystem::path(directory) / "database.sqlite3";
    if (!std::filesystem::exists(inputPath)) {
        throw std::runtime_error("database.sqlite3 not found in backup directory: " + directory);
    }
    sqlite3* source = nullptr;
    if (sqlite3_open_v2(inputPath.string().c_str(), &source, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const std::string message = source == nullptr ? "unknown error" : sqlite3_errmsg(source);
        if (source != nullptr) {
            sqlite3_close(source);
        }
        throw std::runtime_error("Cannot open backup file: " + message);
    }
    sqlite3_backup* backup = sqlite3_backup_init(database_, "main", source, "main");
    if (backup == nullptr) {
        const std::string message = sqlite3_errmsg(database_);
        sqlite3_close(source);
        throw std::runtime_error("Cannot start SQLite restore: " + message);
    }
    const int result = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    sqlite3_close(source);
    if (result != SQLITE_DONE) {
        throw std::runtime_error("SQLite restore did not complete: " + std::string(sqlite3_errmsg(database_)));
    }
}

void SqliteDatabase::seedDemoData() {
    std::scoped_lock lock(mutex_);
    executeScript(Common::readTextFile(seedPath_));
}

// --- Identity ---

Domain::User SqliteDatabase::createUser(const Domain::CreateUserRequest& request,
                                        const std::string& passwordHash) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        const std::string userId = Common::uuidV4();
        Statement insertUser(database_, R"SQL(
INSERT INTO users(id, email, display_name, handle, is_admin, created_at, updated_at)
VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
        insertUser.bind(1, userId);
        insertUser.bind(2, request.email);
        insertUser.bind(3, request.displayName);
        if (request.handle) {
            insertUser.bind(4, *request.handle);
        } else {
            insertUser.bindNull(4);
        }
        insertUser.bind(5, static_cast<std::int64_t>(request.isAdmin ? 1 : 0));
        if (insertUser.step() != SQLITE_DONE) {
            // Email is pre-checked by AuthService; a handle collision is the
            // only other realistic cause of this constraint failure here.
            throw std::invalid_argument(
                request.handle ? "Email or handle is already in use" : "Email is already in use: " + request.email);
        }

        Statement insertCredentials(database_, R"SQL(
INSERT INTO local_credentials(user_id, password_hash, created_at, updated_at)
VALUES (?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
        insertCredentials.bind(1, userId);
        insertCredentials.bind(2, passwordHash);
        expectDone(database_, insertCredentials, "Insert local credentials");

        executeScript("COMMIT;");

        Statement read(database_, std::string(UserSelect) + " WHERE id = ?");
        read.bind(1, userId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Created user could not be read back");
        }
        return readUser(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

std::optional<Domain::User> SqliteDatabase::findUserByEmail(const std::string& email) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(UserSelect) + " WHERE email = ?");
    statement.bind(1, email);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readUser(statement.get());
}

std::optional<Domain::User> SqliteDatabase::findUserById(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(UserSelect) + " WHERE id = ?");
    statement.bind(1, userId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readUser(statement.get());
}

std::optional<Domain::User> SqliteDatabase::findUserByHandle(const std::string& handle) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(UserSelect) + " WHERE handle = ?");
    statement.bind(1, handle);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readUser(statement.get());
}

std::vector<Domain::User> SqliteDatabase::listUsers() {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(UserSelect) + " ORDER BY display_name");
    std::vector<Domain::User> users;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        users.push_back(readUser(statement.get()));
    }
    return users;
}

std::optional<std::string> SqliteDatabase::findPasswordHash(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "SELECT password_hash FROM local_credentials WHERE user_id = ?");
    statement.bind(1, userId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return text(statement.get(), 0);
}

void SqliteDatabase::updateUserPreferences(const std::string& userId, const Domain::UpdatePreferencesRequest& request) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE users SET time_zone = ?, clock_format = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?
)SQL");
    statement.bind(1, request.timeZone);
    statement.bind(2, request.clockFormat);
    statement.bind(3, userId);
    expectDone(database_, statement, "Update user preferences");
}

void SqliteDatabase::recordFailedLogin(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE local_credentials
SET failed_login_count = failed_login_count + 1,
    locked_until = CASE
        WHEN failed_login_count + 1 >= ? THEN datetime('now', '+15 minutes')
        ELSE locked_until
    END,
    updated_at = CURRENT_TIMESTAMP
WHERE user_id = ?
)SQL");
    statement.bind(1, static_cast<std::int64_t>(IDatabase::MaxFailedLoginAttempts));
    statement.bind(2, userId);
    expectDone(database_, statement, "Record failed login");
}

void SqliteDatabase::resetFailedLogin(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE local_credentials
SET failed_login_count = 0, locked_until = NULL, updated_at = CURRENT_TIMESTAMP
WHERE user_id = ?
)SQL");
    statement.bind(1, userId);
    expectDone(database_, statement, "Reset failed login");
}

bool SqliteDatabase::isLoginLocked(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_,
                        "SELECT 1 FROM local_credentials WHERE user_id = ? AND locked_until IS NOT NULL AND locked_until > CURRENT_TIMESTAMP");
    statement.bind(1, userId);
    return statement.step() == SQLITE_ROW;
}

Domain::Session SqliteDatabase::createSession(const std::string& userId,
                                              const std::string& tokenHash,
                                              const std::string& expiresAtIso8601) {
    std::scoped_lock lock(mutex_);
    const std::string sessionId = Common::uuidV4();
    Statement insert(database_, R"SQL(
INSERT INTO sessions(id, user_id, token_hash, created_at, expires_at)
VALUES (?, ?, ?, CURRENT_TIMESTAMP, ?)
)SQL");
    insert.bind(1, sessionId);
    insert.bind(2, userId);
    insert.bind(3, tokenHash);
    insert.bind(4, expiresAtIso8601);
    expectDone(database_, insert, "Create session");

    Statement read(database_, "SELECT id, user_id, created_at, expires_at FROM sessions WHERE id = ?");
    read.bind(1, sessionId);
    if (read.step() != SQLITE_ROW) {
        throw std::runtime_error("Created session could not be read back");
    }
    return Domain::Session{text(read.get(), 0), text(read.get(), 1), text(read.get(), 2), text(read.get(), 3)};
}

std::optional<Domain::Session> SqliteDatabase::findSessionByTokenHash(const std::string& tokenHash) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT id, user_id, created_at, expires_at
FROM sessions
WHERE token_hash = ? AND expires_at > CURRENT_TIMESTAMP
)SQL");
    statement.bind(1, tokenHash);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return Domain::Session{
        text(statement.get(), 0), text(statement.get(), 1), text(statement.get(), 2), text(statement.get(), 3)};
}

void SqliteDatabase::deleteSession(const std::string& sessionId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "DELETE FROM sessions WHERE id = ?");
    statement.bind(1, sessionId);
    expectDone(database_, statement, "Delete session");
}

void SqliteDatabase::deleteExpiredSessions() {
    std::scoped_lock lock(mutex_);
    executeScript("DELETE FROM sessions WHERE expires_at <= CURRENT_TIMESTAMP;");
}

std::vector<Domain::Session> SqliteDatabase::listSessionsForUser(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT id, user_id, created_at, expires_at FROM sessions
WHERE user_id = ? AND expires_at > CURRENT_TIMESTAMP
ORDER BY created_at DESC
)SQL");
    statement.bind(1, userId);
    std::vector<Domain::Session> sessions;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        sessions.push_back(Domain::Session{
            text(statement.get(), 0), text(statement.get(), 1), text(statement.get(), 2), text(statement.get(), 3)});
    }
    return sessions;
}

int SqliteDatabase::deleteOtherSessionsForUser(const std::string& userId, const std::string& keepSessionId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "DELETE FROM sessions WHERE user_id = ?1 AND id <> ?2");
    statement.bind(1, userId);
    statement.bind(2, keepSessionId);
    statement.step();
    return sqlite3_changes(database_);
}

namespace {
Domain::PersonalAccessToken readPersonalAccessToken(sqlite3_stmt* statement) {
    Domain::PersonalAccessToken token;
    token.id = text(statement, 0);
    token.userId = text(statement, 1);
    token.name = text(statement, 2);
    token.createdAt = text(statement, 3);
    token.expiresAt = text(statement, 4);
    token.lastUsedAt = optionalText(statement, 5);
    token.revokedAt = optionalText(statement, 6);
    return token;
}
constexpr const char* PersonalAccessTokenSelect =
    "SELECT id, user_id, name, created_at, expires_at, last_used_at, revoked_at FROM personal_access_tokens";
} // namespace

Domain::PersonalAccessToken SqliteDatabase::createPersonalAccessToken(const std::string& userId,
                                                                       const std::string& name,
                                                                       const std::string& tokenHash,
                                                                       const std::string& expiresAtIso8601) {
    std::scoped_lock lock(mutex_);
    const std::string tokenId = Common::uuidV4();
    Statement insert(database_, R"SQL(
INSERT INTO personal_access_tokens(id, user_id, name, token_hash, created_at, expires_at)
VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, ?)
)SQL");
    insert.bind(1, tokenId);
    insert.bind(2, userId);
    insert.bind(3, name);
    insert.bind(4, tokenHash);
    insert.bind(5, expiresAtIso8601);
    expectDone(database_, insert, "Create personal access token");

    Statement read(database_, std::string(PersonalAccessTokenSelect) + " WHERE id = ?");
    read.bind(1, tokenId);
    if (read.step() != SQLITE_ROW) {
        throw std::runtime_error("Created personal access token could not be read back");
    }
    return readPersonalAccessToken(read.get());
}

std::optional<Domain::PersonalAccessToken> SqliteDatabase::findPersonalAccessTokenByHash(const std::string& tokenHash) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(PersonalAccessTokenSelect)
        + " WHERE token_hash = ? AND expires_at > CURRENT_TIMESTAMP AND revoked_at IS NULL");
    statement.bind(1, tokenHash);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readPersonalAccessToken(statement.get());
}

std::vector<Domain::PersonalAccessToken> SqliteDatabase::listPersonalAccessTokens(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(PersonalAccessTokenSelect) + " WHERE user_id = ? ORDER BY created_at DESC");
    statement.bind(1, userId);
    std::vector<Domain::PersonalAccessToken> tokens;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        tokens.push_back(readPersonalAccessToken(statement.get()));
    }
    return tokens;
}

bool SqliteDatabase::revokePersonalAccessToken(const std::string& tokenId, const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE personal_access_tokens SET revoked_at = CURRENT_TIMESTAMP
WHERE id = ?1 AND user_id = ?2 AND revoked_at IS NULL
)SQL");
    statement.bind(1, tokenId);
    statement.bind(2, userId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

void SqliteDatabase::touchPersonalAccessTokenLastUsed(const std::string& tokenId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "UPDATE personal_access_tokens SET last_used_at = CURRENT_TIMESTAMP WHERE id = ?");
    statement.bind(1, tokenId);
    statement.step();
}

// --- Authorization and project lifecycle ---

namespace {
constexpr const char* ProjectSelectSql = R"SQL(
SELECT p.id, p.project_key, p.name, p.description,
       lead.id, lead.display_name, lead.email,
       COUNT(i.id),
       SUM(CASE WHEN s.category <> 'done' THEN 1 ELSE 0 END),
       p.archived
FROM projects p
LEFT JOIN users lead ON lead.id = p.lead_user_id
LEFT JOIN tickets i ON i.project_id = p.id AND i.deleted_at IS NULL
LEFT JOIN ticket_statuses s ON s.id = i.status_id
)SQL";
} // namespace

std::optional<std::string> SqliteDatabase::findProjectRoleByKey(const std::string& projectKey,
                                                                 const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT pm.role_key
FROM project_members pm
JOIN projects p ON p.id = pm.project_id
WHERE p.project_key = ? AND pm.user_id = ?
)SQL");
    statement.bind(1, projectKey);
    statement.bind(2, userId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return text(statement.get(), 0);
}

Domain::Project SqliteDatabase::createProject(const Domain::CreateProjectRequest& request,
                                              const std::string& creatorUserId) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        const std::string projectId = Common::uuidV4();
        Statement insert(database_, R"SQL(
INSERT INTO projects(id, project_key, name, description, lead_user_id, next_ticket_number, archived, created_at, updated_at)
VALUES (?, ?, ?, ?, ?, 1, 0, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
        insert.bind(1, projectId);
        insert.bind(2, request.key);
        insert.bind(3, request.name);
        insert.bind(4, request.description);
        insert.bind(5, creatorUserId);
        if (insert.step() != SQLITE_DONE) {
            throw std::invalid_argument("Project key is already in use: " + request.key);
        }

        Statement member(database_, "INSERT INTO project_members(project_id, user_id, role_key) VALUES (?, ?, ?)");
        member.bind(1, projectId);
        member.bind(2, creatorUserId);
        member.bind(3, std::string(Domain::ProjectRoleAdmin));
        expectDone(database_, member, "Insert project creator membership");

        executeScript("COMMIT;");

        Statement read(database_, std::string(ProjectSelectSql) + " WHERE p.id = ? GROUP BY p.id");
        read.bind(1, projectId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Created project could not be read back");
        }
        Domain::Project project;
        project.id = text(read.get(), 0);
        project.key = text(read.get(), 1);
        project.name = text(read.get(), 2);
        project.description = text(read.get(), 3);
        if (sqlite3_column_type(read.get(), 4) != SQLITE_NULL) {
            project.lead = readUserSummary(read.get(), 4);
        }
        project.ticketCount = sqlite3_column_int64(read.get(), 7);
        project.openTicketCount = sqlite3_column_int64(read.get(), 8);
        project.archived = boolColumn(read.get(), 9);
        return project;
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SqliteDatabase::setProjectArchived(const std::string& projectKey, const bool archived) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE projects SET archived = ?, archived_at = CASE WHEN ? THEN CURRENT_TIMESTAMP ELSE NULL END, updated_at = CURRENT_TIMESTAMP
WHERE project_key = ? AND deleted_at IS NULL
)SQL");
    statement.bind(1, static_cast<std::int64_t>(archived ? 1 : 0));
    statement.bind(2, static_cast<std::int64_t>(archived ? 1 : 0));
    statement.bind(3, projectKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::optional<Domain::Project> SqliteDatabase::changeProjectKey(const std::string& oldKey, const std::string& newKey) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement projectRow(database_, "SELECT id FROM projects WHERE project_key = ? AND deleted_at IS NULL");
        projectRow.bind(1, oldKey);
        if (projectRow.step() != SQLITE_ROW) {
            executeScript("ROLLBACK;");
            return std::nullopt;
        }
        const std::string projectId = text(projectRow.get(), 0);

        Statement collision(database_, R"SQL(
SELECT 1 FROM projects WHERE project_key = ? AND deleted_at IS NULL
UNION ALL
SELECT 1 FROM project_key_aliases WHERE alias_key = ?
)SQL");
        collision.bind(1, newKey);
        collision.bind(2, newKey);
        if (collision.step() == SQLITE_ROW) {
            throw std::invalid_argument("Project key is already in use: " + newKey);
        }

        // The old key becomes a permanent alias -- same mechanism moveTicket
        // already established for ticket_key_aliases (D38); safe because
        // project_key_aliases.alias_key is itself a PRIMARY KEY.
        Statement projectAlias(database_, "INSERT INTO project_key_aliases(alias_key, project_id) VALUES (?, ?)");
        projectAlias.bind(1, oldKey);
        projectAlias.bind(2, projectId);
        expectDone(database_, projectAlias, "Project key alias insert");

        Statement updateProject(database_, "UPDATE projects SET project_key = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
        updateProject.bind(1, newKey);
        updateProject.bind(2, projectId);
        expectDone(database_, updateProject, "Project key update");

        // Every ticket ever created under this project -- including
        // soft-deleted ones, since a key must stay permanently resolvable
        // regardless of the ticket's own lifecycle state -- is renamed to
        // the new prefix with the same numeric suffix; its own old key
        // becomes a ticket_key_aliases entry, mirroring moveTicket's
        // single-ticket case.
        struct TicketKeyRow {
            std::string ticketId;
            std::int64_t ticketNumber;
            std::string oldTicketKey;
        };
        std::vector<TicketKeyRow> ticketRows;
        Statement tickets(database_, "SELECT id, ticket_number, ticket_key FROM tickets WHERE project_id = ?");
        tickets.bind(1, projectId);
        for (int result = tickets.step(); result == SQLITE_ROW; result = tickets.step()) {
            ticketRows.push_back({text(tickets.get(), 0), sqlite3_column_int64(tickets.get(), 1), text(tickets.get(), 2)});
        }
        for (const auto& row : ticketRows) {
            const std::string newTicketKey = newKey + "-" + std::to_string(row.ticketNumber);
            Statement ticketAlias(database_, "INSERT INTO ticket_key_aliases(alias_key, ticket_id) VALUES (?, ?)");
            ticketAlias.bind(1, row.oldTicketKey);
            ticketAlias.bind(2, row.ticketId);
            expectDone(database_, ticketAlias, "Ticket key alias insert");

            Statement updateTicket(database_, "UPDATE tickets SET ticket_key = ? WHERE id = ?");
            updateTicket.bind(1, newTicketKey);
            updateTicket.bind(2, row.ticketId);
            expectDone(database_, updateTicket, "Ticket key rename");
        }

        executeScript("COMMIT;");
        Statement read(database_, std::string(ProjectSelectSql) + " WHERE p.id = ? GROUP BY p.id");
        read.bind(1, projectId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Renamed project could not be read back");
        }
        Domain::Project project;
        project.id = text(read.get(), 0);
        project.key = text(read.get(), 1);
        project.name = text(read.get(), 2);
        project.description = text(read.get(), 3);
        if (sqlite3_column_type(read.get(), 4) != SQLITE_NULL) {
            project.lead = readUserSummary(read.get(), 4);
        }
        project.ticketCount = sqlite3_column_int64(read.get(), 7);
        project.openTicketCount = sqlite3_column_int64(read.get(), 8);
        project.archived = boolColumn(read.get(), 9);
        return project;
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SqliteDatabase::softDeleteProject(const std::string& projectKey, const std::string& actorUserId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE projects SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = ?, updated_at = CURRENT_TIMESTAMP
WHERE project_key = ? AND deleted_at IS NULL
)SQL");
    statement.bind(1, actorUserId);
    statement.bind(2, projectKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

bool SqliteDatabase::restoreProject(const std::string& projectKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE projects SET deleted_at = NULL, deleted_by_user_id = NULL, updated_at = CURRENT_TIMESTAMP
WHERE project_key = ? AND deleted_at IS NOT NULL
)SQL");
    statement.bind(1, projectKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::vector<Domain::Project> SqliteDatabase::listDeletedProjects() {
    std::scoped_lock lock(mutex_);
    // Fixed 90-day retention, checked on demand -- there is no background
    // job to purge proactively (D89/D51).
    executeScript("DELETE FROM projects WHERE deleted_at IS NOT NULL AND deleted_at <= datetime('now', '-90 days');");

    Statement statement(database_, std::string(ProjectSelectSql) + " WHERE p.deleted_at IS NOT NULL GROUP BY p.id ORDER BY p.deleted_at DESC");
    std::vector<Domain::Project> projects;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        Domain::Project project;
        project.id = text(statement.get(), 0);
        project.key = text(statement.get(), 1);
        project.name = text(statement.get(), 2);
        project.description = text(statement.get(), 3);
        if (sqlite3_column_type(statement.get(), 4) != SQLITE_NULL) {
            project.lead = readUserSummary(statement.get(), 4);
        }
        project.ticketCount = sqlite3_column_int64(statement.get(), 7);
        project.openTicketCount = sqlite3_column_int64(statement.get(), 8);
        project.archived = boolColumn(statement.get(), 9);
        projects.push_back(std::move(project));
    }
    return projects;
}

std::vector<Domain::Project> SqliteDatabase::listArchivedProjects() {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(ProjectSelectSql)
        + " WHERE p.archived = 1 AND p.deleted_at IS NULL GROUP BY p.id ORDER BY p.name");
    std::vector<Domain::Project> projects;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        Domain::Project project;
        project.id = text(statement.get(), 0);
        project.key = text(statement.get(), 1);
        project.name = text(statement.get(), 2);
        project.description = text(statement.get(), 3);
        if (sqlite3_column_type(statement.get(), 4) != SQLITE_NULL) {
            project.lead = readUserSummary(statement.get(), 4);
        }
        project.ticketCount = sqlite3_column_int64(statement.get(), 7);
        project.openTicketCount = sqlite3_column_int64(statement.get(), 8);
        project.archived = boolColumn(statement.get(), 9);
        projects.push_back(std::move(project));
    }
    return projects;
}

bool SqliteDatabase::permanentlyDeleteProject(const std::string& projectKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "DELETE FROM projects WHERE project_key = ? AND deleted_at IS NOT NULL");
    statement.bind(1, projectKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::optional<std::string> SqliteDatabase::getSetting(const std::string& key) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "SELECT value FROM installation_settings WHERE setting_key = ?");
    statement.bind(1, key);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return text(statement.get(), 0);
}

void SqliteDatabase::setSetting(const std::string& key, const std::string& value) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
INSERT INTO installation_settings(setting_key, value, updated_at) VALUES (?, ?, CURRENT_TIMESTAMP)
ON CONFLICT(setting_key) DO UPDATE SET value = excluded.value, updated_at = CURRENT_TIMESTAMP
)SQL");
    statement.bind(1, key);
    statement.bind(2, value);
    expectDone(database_, statement, "Set installation setting");
}

// --- Project components (D19) ---

std::vector<Domain::ProjectComponent> SqliteDatabase::listComponents(const std::string& projectKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(ComponentSelect) + " WHERE p.project_key = ? ORDER BY c.name");
    statement.bind(1, projectKey);
    std::vector<Domain::ProjectComponent> components;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        components.push_back(readComponent(statement.get()));
    }
    return components;
}

Domain::ProjectComponent SqliteDatabase::createComponent(const Domain::CreateComponentRequest& request) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        const std::string projectId = lookupId(database_, "projects", "project_key", request.projectKey);
        std::optional<std::string> leadId;
        if (request.leadEmail.has_value() && !request.leadEmail->empty()) {
            leadId = lookupId(database_, "users", "email", *request.leadEmail);
        }
        std::optional<std::string> defaultAssigneeId;
        if (request.defaultAssigneeEmail.has_value() && !request.defaultAssigneeEmail->empty()) {
            defaultAssigneeId = lookupId(database_, "users", "email", *request.defaultAssigneeEmail);
        }

        const std::string componentId = Common::uuidV4();
        Statement insert(database_, R"SQL(
INSERT INTO project_components(id, project_id, name, description, lead_user_id, default_assignee_user_id, created_at, updated_at)
VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
        insert.bind(1, componentId);
        insert.bind(2, projectId);
        insert.bind(3, request.name);
        insert.bind(4, request.description);
        leadId ? insert.bind(5, *leadId) : insert.bindNull(5);
        defaultAssigneeId ? insert.bind(6, *defaultAssigneeId) : insert.bindNull(6);
        if (insert.step() != SQLITE_DONE) {
            throw std::invalid_argument("Component name is already in use in this project: " + request.name);
        }

        executeScript("COMMIT;");
        Statement read(database_, std::string(ComponentSelect) + " WHERE c.id = ?");
        read.bind(1, componentId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Created component could not be read back");
        }
        return readComponent(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

std::optional<Domain::ProjectComponent> SqliteDatabase::findComponentById(const std::string& componentId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(ComponentSelect) + " WHERE c.id = ?");
    statement.bind(1, componentId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readComponent(statement.get());
}

std::optional<Domain::ProjectComponent> SqliteDatabase::editComponent(const std::string& componentId,
                                                                       const Domain::EditComponentRequest& request) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement exists(database_, "SELECT 1 FROM project_components WHERE id = ?");
        exists.bind(1, componentId);
        if (exists.step() != SQLITE_ROW) {
            executeScript("ROLLBACK;");
            return std::nullopt;
        }

        std::optional<std::string> leadId;
        if (request.leadEmail.has_value() && !request.leadEmail->empty()) {
            leadId = lookupId(database_, "users", "email", *request.leadEmail);
        }
        std::optional<std::string> defaultAssigneeId;
        if (request.defaultAssigneeEmail.has_value() && !request.defaultAssigneeEmail->empty()) {
            defaultAssigneeId = lookupId(database_, "users", "email", *request.defaultAssigneeEmail);
        }

        Statement update(database_, R"SQL(
UPDATE project_components
SET name = ?, description = ?, lead_user_id = ?, default_assignee_user_id = ?, updated_at = CURRENT_TIMESTAMP
WHERE id = ?
)SQL");
        update.bind(1, request.name);
        update.bind(2, request.description);
        leadId ? update.bind(3, *leadId) : update.bindNull(3);
        defaultAssigneeId ? update.bind(4, *defaultAssigneeId) : update.bindNull(4);
        update.bind(5, componentId);
        if (update.step() != SQLITE_DONE) {
            throw std::invalid_argument("Component name is already in use in this project: " + request.name);
        }

        executeScript("COMMIT;");
        Statement read(database_, std::string(ComponentSelect) + " WHERE c.id = ?");
        read.bind(1, componentId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Edited component could not be read back");
        }
        return readComponent(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SqliteDatabase::deleteComponent(const std::string& componentId) {
    std::scoped_lock lock(mutex_);
    // No recycle bin (D19 does not call for one, unlike tickets/projects) --
    // any ticket referencing this component has it cleared via the
    // component_id column's ON DELETE SET NULL, not rejected or cascaded.
    Statement statement(database_, "DELETE FROM project_components WHERE id = ?");
    statement.bind(1, componentId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::vector<Domain::CustomFieldDefinition> SqliteDatabase::listCustomFields(const std::string& projectKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(CustomFieldSelect) + " WHERE p.project_key = ? ORDER BY f.sort_order, f.name");
    statement.bind(1, projectKey);
    std::vector<Domain::CustomFieldDefinition> fields;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        fields.push_back(readCustomFieldDefinition(statement.get()));
    }
    return fields;
}

Domain::CustomFieldDefinition SqliteDatabase::createCustomField(const Domain::CreateCustomFieldRequest& request) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        const std::string projectId = lookupId(database_, "projects", "project_key", request.projectKey);

        Statement maxOrder(database_, "SELECT COALESCE(MAX(sort_order), -1) FROM custom_fields WHERE project_id = ?");
        maxOrder.bind(1, projectId);
        maxOrder.step();
        const int nextOrder = sqlite3_column_int(maxOrder.get(), 0) + 1;

        const std::string fieldId = Common::uuidV4();
        Statement insert(database_, R"SQL(
INSERT INTO custom_fields(id, project_id, name, field_type, options, required, sort_order, created_at)
VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
)SQL");
        insert.bind(1, fieldId);
        insert.bind(2, projectId);
        insert.bind(3, request.name);
        insert.bind(4, request.fieldType);
        insert.bind(5, encodeCustomFieldOptions(request.options));
        insert.bind(6, static_cast<std::int64_t>(request.required ? 1 : 0));
        insert.bind(7, static_cast<std::int64_t>(nextOrder));
        if (insert.step() != SQLITE_DONE) {
            throw std::invalid_argument("Custom field name is already in use in this project: " + request.name);
        }

        executeScript("COMMIT;");
        Statement read(database_, std::string(CustomFieldSelect) + " WHERE f.id = ?");
        read.bind(1, fieldId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Created custom field could not be read back");
        }
        return readCustomFieldDefinition(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

std::optional<Domain::CustomFieldDefinition> SqliteDatabase::findCustomFieldById(const std::string& fieldId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(CustomFieldSelect) + " WHERE f.id = ?");
    statement.bind(1, fieldId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readCustomFieldDefinition(statement.get());
}

std::optional<Domain::CustomFieldDefinition> SqliteDatabase::editCustomField(const std::string& fieldId,
                                                                              const Domain::EditCustomFieldRequest& request) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement exists(database_, "SELECT 1 FROM custom_fields WHERE id = ?");
        exists.bind(1, fieldId);
        if (exists.step() != SQLITE_ROW) {
            executeScript("ROLLBACK;");
            return std::nullopt;
        }

        Statement update(database_, R"SQL(
UPDATE custom_fields SET name = ?, options = ?, required = ?, sort_order = ? WHERE id = ?
)SQL");
        update.bind(1, request.name);
        update.bind(2, encodeCustomFieldOptions(request.options));
        update.bind(3, static_cast<std::int64_t>(request.required ? 1 : 0));
        update.bind(4, static_cast<std::int64_t>(request.sortOrder));
        update.bind(5, fieldId);
        if (update.step() != SQLITE_DONE) {
            throw std::invalid_argument("Custom field name is already in use in this project: " + request.name);
        }

        executeScript("COMMIT;");
        Statement read(database_, std::string(CustomFieldSelect) + " WHERE f.id = ?");
        read.bind(1, fieldId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Edited custom field could not be read back");
        }
        return readCustomFieldDefinition(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SqliteDatabase::deleteCustomField(const std::string& fieldId) {
    std::scoped_lock lock(mutex_);
    // No recycle bin (matches project components, D19) -- every stored
    // value for this field is cascaded away via ticket_custom_field_values'
    // ON DELETE CASCADE on field_id.
    Statement statement(database_, "DELETE FROM custom_fields WHERE id = ?");
    statement.bind(1, fieldId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::vector<Domain::CustomFieldValue> SqliteDatabase::listTicketCustomFieldValues(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    // One row per field defined on the ticket's project, whether or not a
    // value has ever been stored for it (LEFT JOIN), so the caller can
    // render every field -- including unset ones -- on the ticket view.
    Statement statement(database_, R"SQL(
SELECT f.id, f.name, f.field_type, v.value
FROM custom_fields f
JOIN tickets i ON i.project_id = f.project_id
LEFT JOIN ticket_custom_field_values v ON v.field_id = f.id AND v.ticket_id = i.id
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1))
ORDER BY f.sort_order, f.name
)SQL");
    statement.bind(1, ticketKey);
    std::vector<Domain::CustomFieldValue> values;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        Domain::CustomFieldValue value;
        value.fieldId = text(statement.get(), 0);
        value.name = text(statement.get(), 1);
        value.fieldType = text(statement.get(), 2);
        value.value = optionalText(statement.get(), 3);
        values.push_back(std::move(value));
    }
    return values;
}

namespace {
void applyTicketCustomFieldValues(sqlite3* database, const std::string& ticketId, const std::string& projectId,
                                   const std::vector<Domain::CustomFieldValueInput>& values) {
    // Called only from within an already-open transaction (createTicket/
    // editTicket), so this does not itself BEGIN/COMMIT -- matching how
    // those methods already handle labels/component inline. A free
    // function taking the raw database handle, matching lookupId/
    // lookupComponentId's existing pattern for this kind of internal helper.
    // Anonymous-namespace-scoped (internal linkage) since PostgresDatabase.cpp
    // defines its own same-named equivalent for its own PGconn type.
    Statement clear(database, "DELETE FROM ticket_custom_field_values WHERE ticket_id = ?");
    clear.bind(1, ticketId);
    expectDone(database, clear, "Clear ticket custom field values");
    for (const auto& input : values) {
        if (input.value.empty()) {
            continue;
        }
        // The SELECT scopes field_id to this ticket's own project -- a
        // fieldId that exists but belongs to a different project matches
        // no row, so nothing is inserted and the changes() check below
        // rejects it the same way an entirely unknown id would.
        Statement insert(database, R"SQL(
INSERT INTO ticket_custom_field_values(ticket_id, field_id, value)
SELECT ?, id, ? FROM custom_fields WHERE id = ? AND project_id = ?
)SQL");
        insert.bind(1, ticketId);
        insert.bind(2, input.value);
        insert.bind(3, input.fieldId);
        insert.bind(4, projectId);
        expectDone(database, insert, "Ticket custom field value insert");
        if (sqlite3_changes(database) == 0) {
            throw std::invalid_argument("Unknown custom field id for this project: " + input.fieldId);
        }
    }
}
} // namespace

// --- Ticket tracker ---

std::vector<Domain::Project> SqliteDatabase::listProjects() {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT p.id, p.project_key, p.name, p.description,
       lead.id, lead.display_name, lead.email,
       COUNT(i.id),
       SUM(CASE WHEN s.category <> 'done' THEN 1 ELSE 0 END)
FROM projects p
LEFT JOIN users lead ON lead.id = p.lead_user_id
LEFT JOIN tickets i ON i.project_id = p.id AND i.deleted_at IS NULL
LEFT JOIN ticket_statuses s ON s.id = i.status_id
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
            project.lead = readUserSummary(statement.get(), 4);
        }
        project.ticketCount = sqlite3_column_int64(statement.get(), 7);
        project.openTicketCount = sqlite3_column_int64(statement.get(), 8);
        projects.push_back(std::move(project));
    }
    return projects;
}

std::vector<Domain::Ticket> SqliteDatabase::listTickets(const Domain::TicketFilter& filter) {
    std::scoped_lock lock(mutex_);
    // `label` is checked via EXISTS rather than the already-joined/aggregated
    // ticket_labels/labels (which feed the GROUP_CONCAT summary column) --
    // filtering on the joined row directly would silently drop every other
    // label the ticket has from that GROUP_CONCAT.
    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND (?1 IS NULL OR p.project_key = ?1)
  AND (?2 IS NULL OR s.status_key = ?2)
  AND (?3 IS NULL OR it.type_key = ?3)
  AND (?4 IS NULL OR pr.priority_key = ?4)
  AND (?5 IS NULL OR assignee.email = ?5)
  AND (?6 IS NULL OR i.due_date <= ?6)
  AND (?7 IS NULL OR EXISTS (
        SELECT 1 FROM ticket_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.ticket_id = i.id AND LOWER(l2.name) = LOWER(?7)))
  AND (?8 IS NULL OR LOWER(i.summary) LIKE LOWER(?8) OR LOWER(i.description) LIKE LOWER(?8)
       OR LOWER(i.ticket_key) LIKE LOWER(?8))
  AND (?9 IS NULL OR LOWER(comp.name) = LOWER(?9))
GROUP BY i.id
)SQL" + (filter.sortByRank ? "ORDER BY i.rank_order, i.ticket_number\n" : "ORDER BY i.updated_at DESC, i.ticket_key DESC\n") + R"SQL(
LIMIT 200
)SQL";
    Statement statement(database_, sql);
    filter.projectKey ? statement.bind(1, *filter.projectKey) : statement.bindNull(1);
    filter.statusKey ? statement.bind(2, *filter.statusKey) : statement.bindNull(2);
    filter.ticketTypeKey ? statement.bind(3, *filter.ticketTypeKey) : statement.bindNull(3);
    filter.priorityKey ? statement.bind(4, *filter.priorityKey) : statement.bindNull(4);
    filter.assigneeEmail ? statement.bind(5, *filter.assigneeEmail) : statement.bindNull(5);
    filter.dueBefore ? statement.bind(6, *filter.dueBefore) : statement.bindNull(6);
    filter.label ? statement.bind(7, *filter.label) : statement.bindNull(7);
    filter.search ? statement.bind(8, "%" + *filter.search + "%") : statement.bindNull(8);
    filter.componentName ? statement.bind(9, *filter.componentName) : statement.bindNull(9);

    std::vector<Domain::Ticket> tickets;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        tickets.push_back(readTicket(statement.get()));
    }
    return tickets;
}

std::vector<Domain::Ticket> SqliteDatabase::listTickets(const Domain::TicketFilter& filter, int limit, int offset) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND (?1 IS NULL OR p.project_key = ?1)
  AND (?2 IS NULL OR s.status_key = ?2)
  AND (?3 IS NULL OR it.type_key = ?3)
  AND (?4 IS NULL OR pr.priority_key = ?4)
  AND (?5 IS NULL OR assignee.email = ?5)
  AND (?6 IS NULL OR i.due_date <= ?6)
  AND (?7 IS NULL OR EXISTS (
        SELECT 1 FROM ticket_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.ticket_id = i.id AND LOWER(l2.name) = LOWER(?7)))
  AND (?8 IS NULL OR LOWER(i.summary) LIKE LOWER(?8) OR LOWER(i.description) LIKE LOWER(?8)
       OR LOWER(i.ticket_key) LIKE LOWER(?8))
  AND (?9 IS NULL OR LOWER(comp.name) = LOWER(?9))
GROUP BY i.id
)SQL" + (filter.sortByRank ? "ORDER BY i.rank_order, i.ticket_number\n" : "ORDER BY i.updated_at DESC, i.ticket_key DESC\n") + R"SQL(
LIMIT ?10 OFFSET ?11
)SQL";
    Statement statement(database_, sql);
    filter.projectKey ? statement.bind(1, *filter.projectKey) : statement.bindNull(1);
    filter.statusKey ? statement.bind(2, *filter.statusKey) : statement.bindNull(2);
    filter.ticketTypeKey ? statement.bind(3, *filter.ticketTypeKey) : statement.bindNull(3);
    filter.priorityKey ? statement.bind(4, *filter.priorityKey) : statement.bindNull(4);
    filter.assigneeEmail ? statement.bind(5, *filter.assigneeEmail) : statement.bindNull(5);
    filter.dueBefore ? statement.bind(6, *filter.dueBefore) : statement.bindNull(6);
    filter.label ? statement.bind(7, *filter.label) : statement.bindNull(7);
    filter.search ? statement.bind(8, "%" + *filter.search + "%") : statement.bindNull(8);
    filter.componentName ? statement.bind(9, *filter.componentName) : statement.bindNull(9);
    statement.bind(10, static_cast<std::int64_t>(limit));
    statement.bind(11, static_cast<std::int64_t>(offset));

    std::vector<Domain::Ticket> tickets;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        tickets.push_back(readTicket(statement.get()));
    }
    return tickets;
}

std::int64_t SqliteDatabase::countTickets(const Domain::TicketFilter& filter) {
    std::scoped_lock lock(mutex_);
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
  AND (?1 IS NULL OR p.project_key = ?1)
  AND (?2 IS NULL OR s.status_key = ?2)
  AND (?3 IS NULL OR it.type_key = ?3)
  AND (?4 IS NULL OR pr.priority_key = ?4)
  AND (?5 IS NULL OR assignee.email = ?5)
  AND (?6 IS NULL OR i.due_date <= ?6)
  AND (?7 IS NULL OR EXISTS (
        SELECT 1 FROM ticket_labels il2 JOIN labels l2 ON l2.id = il2.label_id
        WHERE il2.ticket_id = i.id AND LOWER(l2.name) = LOWER(?7)))
  AND (?8 IS NULL OR LOWER(i.summary) LIKE LOWER(?8) OR LOWER(i.description) LIKE LOWER(?8)
       OR LOWER(i.ticket_key) LIKE LOWER(?8))
  AND (?9 IS NULL OR LOWER(comp.name) = LOWER(?9))
)SQL";
    Statement statement(database_, sql);
    filter.projectKey ? statement.bind(1, *filter.projectKey) : statement.bindNull(1);
    filter.statusKey ? statement.bind(2, *filter.statusKey) : statement.bindNull(2);
    filter.ticketTypeKey ? statement.bind(3, *filter.ticketTypeKey) : statement.bindNull(3);
    filter.priorityKey ? statement.bind(4, *filter.priorityKey) : statement.bindNull(4);
    filter.assigneeEmail ? statement.bind(5, *filter.assigneeEmail) : statement.bindNull(5);
    filter.dueBefore ? statement.bind(6, *filter.dueBefore) : statement.bindNull(6);
    filter.label ? statement.bind(7, *filter.label) : statement.bindNull(7);
    filter.search ? statement.bind(8, "%" + *filter.search + "%") : statement.bindNull(8);
    filter.componentName ? statement.bind(9, *filter.componentName) : statement.bindNull(9);

    if (statement.step() != SQLITE_ROW) {
        return 0;
    }
    return sqlite3_column_int64(statement.get(), 0);
}

std::optional<Domain::Ticket> SqliteDatabase::findTicketByKey(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1))
GROUP BY i.id
)SQL";
    Statement statement(database_, sql);
    statement.bind(1, ticketKey);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readTicket(statement.get());
}

Domain::Ticket SqliteDatabase::createTicket(const Domain::CreateTicketRequest& request,
                                          const std::string& reporterUserId) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement projectStatement(database_, "SELECT id, next_ticket_number FROM projects WHERE project_key = ? AND archived = 0 AND deleted_at IS NULL");
        projectStatement.bind(1, request.projectKey);
        if (projectStatement.step() != SQLITE_ROW) {
            throw std::invalid_argument("Unknown project: " + request.projectKey);
        }
        const std::string projectId = text(projectStatement.get(), 0);
        const std::int64_t ticketNumber = sqlite3_column_int64(projectStatement.get(), 1);
        const std::string ticketKey = request.projectKey + "-" + std::to_string(ticketNumber);

        Statement increment(database_, "UPDATE projects SET next_ticket_number = next_ticket_number + 1, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
        increment.bind(1, projectId);
        expectDone(database_, increment, "Project counter update");

        const std::string ticketId = Common::uuidV4();
        const std::string ticketTypeId = lookupId(database_, "ticket_types", "type_key", request.ticketTypeKey);
        const std::string statusId = lookupId(database_, "ticket_statuses", "status_key", "backlog");
        const std::string priorityId = lookupId(database_, "priorities", "priority_key", request.priorityKey);
        const std::string reporterId = requireUserId(database_, reporterUserId);
        std::optional<std::string> assigneeId;
        if (request.assigneeEmail.has_value() && !request.assigneeEmail->empty()) {
            assigneeId = lookupId(database_, "users", "email", *request.assigneeEmail);
        }
        std::optional<std::string> parentId;
        if (request.parentTicketKey.has_value() && !request.parentTicketKey->empty()) {
            parentId = lookupTicketId(database_, *request.parentTicketKey);
        }
        std::optional<std::string> componentId;
        if (request.componentName.has_value() && !request.componentName->empty()) {
            componentId = lookupComponentId(database_, projectId, *request.componentName);
        }

        // Simple integer manual order (D31): new tickets are appended after
        // the highest existing rank within their project.
        std::int64_t rankOrder = 1;
        {
            Statement maxRank(database_, "SELECT COALESCE(MAX(rank_order), 0) + 1 FROM tickets WHERE project_id = ? AND deleted_at IS NULL");
            maxRank.bind(1, projectId);
            if (maxRank.step() == SQLITE_ROW) {
                rankOrder = sqlite3_column_int64(maxRank.get(), 0);
            }
        }

        Statement insert(database_, R"SQL(
INSERT INTO tickets(id, project_id, ticket_number, ticket_key, summary, description,
                   ticket_type_id, status_id, priority_id, reporter_user_id, assignee_user_id,
                   parent_ticket_id, story_points, due_date, rank_order, component_id, created_at, updated_at)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
        insert.bind(1, ticketId);
        insert.bind(2, projectId);
        insert.bind(3, ticketNumber);
        insert.bind(4, ticketKey);
        insert.bind(5, request.summary);
        insert.bind(6, request.description);
        insert.bind(7, ticketTypeId);
        insert.bind(8, statusId);
        insert.bind(9, priorityId);
        insert.bind(10, reporterId);
        assigneeId ? insert.bind(11, *assigneeId) : insert.bindNull(11);
        parentId ? insert.bind(12, *parentId) : insert.bindNull(12);
        request.storyPoints ? insert.bind(13, *request.storyPoints) : insert.bindNull(13);
        request.dueDate ? insert.bind(14, *request.dueDate) : insert.bindNull(14);
        insert.bind(15, rankOrder);
        componentId ? insert.bind(16, *componentId) : insert.bindNull(16);
        expectDone(database_, insert, "Ticket insert");

        for (const auto& labelName : request.labels) {
            const std::string labelId = Common::uuidV4();
            Statement label(database_, "INSERT INTO labels(id, name) VALUES (?, ?) ON CONFLICT(name) DO NOTHING");
            label.bind(1, labelId);
            label.bind(2, labelName);
            expectDone(database_, label, "Label insert");

            Statement link(database_, R"SQL(
INSERT OR IGNORE INTO ticket_labels(ticket_id, label_id)
SELECT ?, id FROM labels WHERE name = ?
)SQL");
            link.bind(1, ticketId);
            link.bind(2, labelName);
            expectDone(database_, link, "Ticket label insert");
        }

        applyTicketCustomFieldValues(database_, ticketId, projectId, request.customFieldValues);

        executeScript("COMMIT;");
        const std::string sql = std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND i.ticket_key = ?1 GROUP BY i.id";
        Statement read(database_, sql);
        read.bind(1, ticketKey);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Created ticket could not be read back");
        }
        return readTicket(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SqliteDatabase::changeTicketStatus(const std::string& ticketKey,
                                       const std::string& statusKey,
                                       const std::string& actorUserId,
                                       const std::optional<std::string> resolution,
                                       const std::optional<std::int64_t> expectedVersion) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement current(database_, R"SQL(
SELECT i.id, s.status_key, s.category, i.version, i.resolution
FROM tickets i JOIN ticket_statuses s ON s.id = i.status_id
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1))
)SQL");
        current.bind(1, ticketKey);
        if (current.step() != SQLITE_ROW) {
            executeScript("ROLLBACK;");
            return false;
        }
        const std::string ticketId = text(current.get(), 0);
        const std::string oldStatus = text(current.get(), 1);
        const std::string oldCategory = text(current.get(), 2);
        const std::int64_t currentVersion = sqlite3_column_int64(current.get(), 3);
        const bool hadResolution = sqlite3_column_type(current.get(), 4) != SQLITE_NULL;
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
            executeScript("COMMIT;");
            return true;
        }

        Statement targetStatus(database_, "SELECT id, category FROM ticket_statuses WHERE status_key = ?");
        targetStatus.bind(1, statusKey);
        if (targetStatus.step() != SQLITE_ROW) {
            throw std::invalid_argument("Unknown ticket_statuses key: " + statusKey);
        }
        const std::string statusId = text(targetStatus.get(), 0);
        const std::string targetCategory = text(targetStatus.get(), 1);
        const std::string actorId = requireUserId(database_, actorUserId);

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
            Statement unfinishedChild(database_, R"SQL(
SELECT 1
FROM tickets child
JOIN ticket_statuses cs ON cs.id = child.status_id
WHERE child.parent_ticket_id = ? AND child.deleted_at IS NULL AND cs.category <> 'done'
LIMIT 1
)SQL");
            unfinishedChild.bind(1, ticketId);
            if (unfinishedChild.step() == SQLITE_ROW) {
                throw Domain::WorkflowViolation("Cannot complete a ticket while it has unfinished sub-tasks");
            }
            touchResolution = true;
            resolutionValue = resolution;
        } else if (oldCategory == "done") {
            touchResolution = true;
            resolutionValue = std::nullopt;
        }

        if (touchResolution) {
            Statement update(database_, R"SQL(
UPDATE tickets SET status_id = ?, resolution = ?, version = version + 1, updated_at = CURRENT_TIMESTAMP WHERE id = ?
)SQL");
            update.bind(1, statusId);
            resolutionValue ? update.bind(2, *resolutionValue) : update.bindNull(2);
            update.bind(3, ticketId);
            expectDone(database_, update, "Ticket status update");
        } else {
            Statement update(database_, "UPDATE tickets SET status_id = ?, version = version + 1, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
            update.bind(1, statusId);
            update.bind(2, ticketId);
            expectDone(database_, update, "Ticket status update");
        }

        Statement history(database_, R"SQL(
INSERT INTO ticket_history(id, ticket_id, actor_user_id, field_name, old_value, new_value)
VALUES (?, ?, ?, 'status', ?, ?)
)SQL");
        history.bind(1, Common::uuidV4());
        history.bind(2, ticketId);
        history.bind(3, actorId);
        history.bind(4, oldStatus);
        history.bind(5, statusKey);
        expectDone(database_, history, "Ticket history insert");
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

namespace {
std::string historyText(const std::optional<std::string>& value) {
    return value.value_or(std::string());
}
std::string historyText(const std::optional<double>& value) {
    return value ? std::to_string(*value) : std::string();
}
} // namespace

std::optional<Domain::Ticket> SqliteDatabase::editTicket(const std::string& ticketKey,
                                                       const Domain::EditTicketRequest& request,
                                                       const std::string& actorUserId,
                                                       const std::optional<std::int64_t> expectedVersion) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement current(database_, R"SQL(
SELECT i.id, i.summary, i.description, pr.priority_key, assignee.email,
       i.story_points, i.due_date, i.version, it.type_key,
       (SELECT ticket_key FROM tickets WHERE id = i.parent_ticket_id),
       i.project_id, comp.name
FROM tickets i
JOIN priorities pr ON pr.id = i.priority_id
JOIN ticket_types it ON it.id = i.ticket_type_id
LEFT JOIN users assignee ON assignee.id = i.assignee_user_id
LEFT JOIN project_components comp ON comp.id = i.component_id
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1))
)SQL");
        current.bind(1, ticketKey);
        if (current.step() != SQLITE_ROW) {
            executeScript("ROLLBACK;");
            return std::nullopt;
        }
        const std::string ticketId = text(current.get(), 0);
        const std::string oldSummary = text(current.get(), 1);
        const std::string oldDescription = text(current.get(), 2);
        const std::string oldPriorityKey = text(current.get(), 3);
        const std::optional<std::string> oldAssigneeEmail = optionalText(current.get(), 4);
        std::optional<double> oldStoryPoints;
        if (sqlite3_column_type(current.get(), 5) != SQLITE_NULL) {
            oldStoryPoints = sqlite3_column_double(current.get(), 5);
        }
        const std::optional<std::string> oldDueDate = optionalText(current.get(), 6);
        const std::int64_t currentVersion = sqlite3_column_int64(current.get(), 7);
        const std::string oldTypeKey = text(current.get(), 8);
        const std::optional<std::string> oldParentKey = optionalText(current.get(), 9);
        const std::string projectId = text(current.get(), 10);
        const std::optional<std::string> oldComponentName = optionalText(current.get(), 11);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Ticket was modified by another user");
        }

        // Re-typing across hierarchy levels (Epic <-> Story/Task/Bug <->
        // Sub-task) is only safe to apply if this ticket currently has no
        // children -- a child's own hierarchy rule ("my parent must be an
        // Epic" / "my parent must be a Story, Task, or Bug") depends on
        // this ticket's *current* level, and cascading a fix to every child
        // is out of scope (mirrors moveTicket's existing "has children"
        // rejection). Same-level retyping (e.g. Task -> Bug) never affects
        // children, since they all share hierarchy level 0. This must be
        // checked transactionally, not in TicketService, since a concurrent
        // insert of a new child between the check and the update would
        // otherwise race past it.
        if (Domain::ticketTypeHierarchyLevel(oldTypeKey) != Domain::ticketTypeHierarchyLevel(request.ticketTypeKey)) {
            Statement childCheck(database_, "SELECT COUNT(*) FROM tickets WHERE parent_ticket_id = ? AND deleted_at IS NULL");
            childCheck.bind(1, ticketId);
            childCheck.step();
            if (sqlite3_column_int64(childCheck.get(), 0) > 0) {
                throw std::invalid_argument(
                    "Cannot change a ticket's type across hierarchy levels while it has child tickets");
            }
        }

        const std::string priorityId = lookupId(database_, "priorities", "priority_key", request.priorityKey);
        std::optional<std::string> assigneeId;
        if (request.assigneeEmail.has_value() && !request.assigneeEmail->empty()) {
            assigneeId = lookupId(database_, "users", "email", *request.assigneeEmail);
        }
        const std::string ticketTypeId = lookupId(database_, "ticket_types", "type_key", request.ticketTypeKey);
        std::optional<std::string> parentId;
        if (request.parentTicketKey.has_value() && !request.parentTicketKey->empty()) {
            parentId = lookupTicketId(database_, *request.parentTicketKey);
        }
        std::optional<std::string> componentId;
        if (request.componentName.has_value() && !request.componentName->empty()) {
            componentId = lookupComponentId(database_, projectId, *request.componentName);
        }
        const std::string actorId = requireUserId(database_, actorUserId);

        Statement update(database_, R"SQL(
UPDATE tickets
SET summary = ?, description = ?, priority_id = ?, assignee_user_id = ?,
    ticket_type_id = ?, parent_ticket_id = ?,
    story_points = ?, due_date = ?, component_id = ?, version = version + 1, updated_at = CURRENT_TIMESTAMP
WHERE id = ?
)SQL");
        update.bind(1, request.summary);
        update.bind(2, request.description);
        update.bind(3, priorityId);
        assigneeId ? update.bind(4, *assigneeId) : update.bindNull(4);
        update.bind(5, ticketTypeId);
        parentId ? update.bind(6, *parentId) : update.bindNull(6);
        request.storyPoints ? update.bind(7, *request.storyPoints) : update.bindNull(7);
        request.dueDate ? update.bind(8, *request.dueDate) : update.bindNull(8);
        componentId ? update.bind(9, *componentId) : update.bindNull(9);
        update.bind(10, ticketId);
        expectDone(database_, update, "Ticket edit update");

        Statement clearLabels(database_, "DELETE FROM ticket_labels WHERE ticket_id = ?");
        clearLabels.bind(1, ticketId);
        expectDone(database_, clearLabels, "Clear ticket labels");
        for (const auto& labelName : request.labels) {
            const std::string labelId = Common::uuidV4();
            Statement label(database_, "INSERT INTO labels(id, name) VALUES (?, ?) ON CONFLICT(name) DO NOTHING");
            label.bind(1, labelId);
            label.bind(2, labelName);
            expectDone(database_, label, "Label insert");

            Statement link(database_, "INSERT OR IGNORE INTO ticket_labels(ticket_id, label_id) SELECT ?, id FROM labels WHERE name = ?");
            link.bind(1, ticketId);
            link.bind(2, labelName);
            expectDone(database_, link, "Ticket label insert");
        }

        applyTicketCustomFieldValues(database_, ticketId, projectId, request.customFieldValues);

        auto recordHistory = [&](const char* field, const std::string& oldValue, const std::string& newValue) {
            if (oldValue == newValue) {
                return;
            }
            Statement history(database_, R"SQL(
INSERT INTO ticket_history(id, ticket_id, actor_user_id, field_name, old_value, new_value)
VALUES (?, ?, ?, ?, ?, ?)
)SQL");
            history.bind(1, Common::uuidV4());
            history.bind(2, ticketId);
            history.bind(3, actorId);
            history.bind(4, std::string(field));
            oldValue.empty() ? history.bindNull(5) : history.bind(5, oldValue);
            newValue.empty() ? history.bindNull(6) : history.bind(6, newValue);
            expectDone(database_, history, "Ticket history insert");
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

        executeScript("COMMIT;");
        const std::string sql = std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND i.id = ?1 GROUP BY i.id";
        Statement read(database_, sql);
        read.bind(1, ticketId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Edited ticket could not be read back");
        }
        return readTicket(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

Domain::Ticket SqliteDatabase::reorderTicket(const std::string& ticketKey,
                                           std::optional<std::string> beforeTicketKey) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        const std::string ticketId = lookupTicketId(database_, ticketKey);
        Statement projectRow(database_, "SELECT project_id FROM tickets WHERE id = ?");
        projectRow.bind(1, ticketId);
        projectRow.step();
        const std::string projectId = text(projectRow.get(), 0);

        std::optional<std::string> beforeTicketId;
        if (beforeTicketKey.has_value() && !beforeTicketKey->empty()) {
            const std::string resolvedBeforeId = lookupTicketId(database_, *beforeTicketKey);
            if (resolvedBeforeId == ticketId) {
                throw std::invalid_argument("Cannot reorder a ticket before itself");
            }
            Statement beforeProjectRow(database_, "SELECT project_id FROM tickets WHERE id = ?");
            beforeProjectRow.bind(1, resolvedBeforeId);
            beforeProjectRow.step();
            if (text(beforeProjectRow.get(), 0) != projectId) {
                throw std::invalid_argument("Cannot reorder relative to a ticket in a different project");
            }
            beforeTicketId = resolvedBeforeId;
        }

        // Full renumbering pass (D31): sufficient for small per-project ticket
        // counts, and simpler than a minimal-diff fractional/shift scheme.
        std::vector<std::string> orderedIds;
        Statement listStatement(database_, "SELECT id FROM tickets WHERE project_id = ? AND deleted_at IS NULL ORDER BY rank_order, ticket_number");
        listStatement.bind(1, projectId);
        for (int result = listStatement.step(); result == SQLITE_ROW; result = listStatement.step()) {
            orderedIds.push_back(text(listStatement.get(), 0));
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
            Statement update(database_, "UPDATE tickets SET rank_order = ? WHERE id = ? AND rank_order <> ?");
            update.bind(1, newRank);
            update.bind(2, orderedIds[index]);
            update.bind(3, newRank);
            expectDone(database_, update, "Ticket rank update");
        }

        executeScript("COMMIT;");
        const std::string sql = std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND i.id = ?1 GROUP BY i.id";
        Statement read(database_, sql);
        read.bind(1, ticketId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Reordered ticket could not be read back");
        }
        return readTicket(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

Domain::Ticket SqliteDatabase::moveTicket(const std::string& ticketKey,
                                        const std::string& targetProjectKey,
                                        const std::string& actorUserId) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        const std::string ticketId = lookupTicketId(database_, ticketKey);
        Statement current(database_, R"SQL(
SELECT i.project_id, p.project_key, i.ticket_key, i.parent_ticket_id
FROM tickets i JOIN projects p ON p.id = i.project_id
WHERE i.id = ?
)SQL");
        current.bind(1, ticketId);
        current.step();
        const std::string currentProjectId = text(current.get(), 0);
        const std::string currentProjectKey = text(current.get(), 1);
        const std::string currentTicketKey = text(current.get(), 2);
        const bool hasParent = sqlite3_column_type(current.get(), 3) != SQLITE_NULL;
        if (hasParent) {
            throw std::invalid_argument("Cannot move a ticket that has a parent");
        }

        Statement childCheck(database_, "SELECT COUNT(*) FROM tickets WHERE parent_ticket_id = ? AND deleted_at IS NULL");
        childCheck.bind(1, ticketId);
        childCheck.step();
        if (sqlite3_column_int64(childCheck.get(), 0) > 0) {
            throw std::invalid_argument("Cannot move a ticket that has child tickets");
        }

        Statement projectRow(database_, "SELECT id, next_ticket_number FROM projects WHERE project_key = ? AND archived = 0 AND deleted_at IS NULL");
        projectRow.bind(1, targetProjectKey);
        if (projectRow.step() != SQLITE_ROW) {
            throw std::invalid_argument("Unknown project: " + targetProjectKey);
        }
        const std::string targetProjectId = text(projectRow.get(), 0);
        if (targetProjectId == currentProjectId) {
            throw std::invalid_argument("Ticket is already in project: " + targetProjectKey);
        }
        const std::int64_t ticketNumber = sqlite3_column_int64(projectRow.get(), 1);
        const std::string newTicketKey = targetProjectKey + "-" + std::to_string(ticketNumber);

        Statement increment(database_, "UPDATE projects SET next_ticket_number = next_ticket_number + 1, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
        increment.bind(1, targetProjectId);
        expectDone(database_, increment, "Project counter update");

        // Append-at-end within the target project, same as createTicket (D31).
        std::int64_t rankOrder = 1;
        {
            Statement maxRank(database_, "SELECT COALESCE(MAX(rank_order), 0) + 1 FROM tickets WHERE project_id = ? AND deleted_at IS NULL");
            maxRank.bind(1, targetProjectId);
            if (maxRank.step() == SQLITE_ROW) {
                rankOrder = sqlite3_column_int64(maxRank.get(), 0);
            }
        }

        Statement update(database_, R"SQL(
UPDATE tickets
SET project_id = ?, ticket_number = ?, ticket_key = ?, rank_order = ?, updated_at = CURRENT_TIMESTAMP
WHERE id = ?
)SQL");
        update.bind(1, targetProjectId);
        update.bind(2, ticketNumber);
        update.bind(3, newTicketKey);
        update.bind(4, rankOrder);
        update.bind(5, ticketId);
        expectDone(database_, update, "Ticket move update");

        // The vacated key becomes a permanent alias (D38); safe because
        // ticket_key_aliases.alias_key is a PRIMARY KEY (no collision) and
        // ticket numbers/keys are never reused.
        Statement alias(database_, "INSERT INTO ticket_key_aliases(alias_key, ticket_id) VALUES (?, ?)");
        alias.bind(1, currentTicketKey);
        alias.bind(2, ticketId);
        expectDone(database_, alias, "Ticket key alias insert");

        const std::string actorId = requireUserId(database_, actorUserId);
        Statement history(database_, R"SQL(
INSERT INTO ticket_history(id, ticket_id, actor_user_id, field_name, old_value, new_value)
VALUES (?, ?, ?, 'project', ?, ?)
)SQL");
        history.bind(1, Common::uuidV4());
        history.bind(2, ticketId);
        history.bind(3, actorId);
        history.bind(4, currentProjectKey);
        history.bind(5, targetProjectKey);
        expectDone(database_, history, "Ticket history insert");

        executeScript("COMMIT;");
        const std::string sql = std::string(TicketSelect) + " WHERE i.deleted_at IS NULL AND i.id = ?1 GROUP BY i.id";
        Statement read(database_, sql);
        read.bind(1, ticketId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Moved ticket could not be read back");
        }
        return readTicket(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

std::vector<Domain::TicketHistoryEntry> SqliteDatabase::listTicketHistory(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(TicketHistorySelect) + R"SQL(
JOIN tickets i ON i.id = h.ticket_id
WHERE i.deleted_at IS NULL
  AND (i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1))
ORDER BY h.created_at DESC, h.id DESC
)SQL";
    Statement statement(database_, sql);
    statement.bind(1, ticketKey);
    std::vector<Domain::TicketHistoryEntry> entries;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        entries.push_back(readTicketHistoryEntry(statement.get()));
    }
    return entries;
}

std::vector<Domain::Comment> SqliteDatabase::listComments(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(CommentSelect) + R"SQL(
JOIN tickets i ON i.id = c.ticket_id
WHERE c.deleted_at IS NULL
  AND i.deleted_at IS NULL
  AND (i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1))
ORDER BY c.created_at
)SQL";
    Statement statement(database_, sql);
    statement.bind(1, ticketKey);
    std::vector<Domain::Comment> comments;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        comments.push_back(readComment(statement.get()));
    }
    return comments;
}

Domain::Comment SqliteDatabase::addComment(const Domain::AddCommentRequest& request,
                                           const std::string& authorUserId) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, request.ticketKey);
    const std::string authorId = requireUserId(database_, authorUserId);
    const std::string commentId = Common::uuidV4();

    Statement insert(database_, R"SQL(
INSERT INTO comments(id, ticket_id, author_user_id, body, created_at, updated_at)
VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, commentId);
    insert.bind(2, ticketId);
    insert.bind(3, authorId);
    insert.bind(4, request.body);
    expectDone(database_, insert, "Comment insert");

    const std::string sql = std::string(CommentSelect) + "WHERE c.id = ?";
    Statement read(database_, sql);
    read.bind(1, commentId);
    if (read.step() != SQLITE_ROW) {
        throw std::runtime_error("Created comment could not be read back");
    }
    return readComment(read.get());
}

std::optional<Domain::Comment> SqliteDatabase::findCommentById(const std::string& commentId) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(CommentSelect) + "WHERE c.id = ? AND c.deleted_at IS NULL";
    Statement statement(database_, sql);
    statement.bind(1, commentId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readComment(statement.get());
}

// `actorUserId` is unused: D81's simplified edited-flag schema has no
// per-edit actor column (unlike ticket_history) -- only `edited_at` is
// tracked. Kept in the signature for symmetry with editTicket and in case a
// future decision adds an `edited_by_user_id` column.
std::optional<Domain::Comment> SqliteDatabase::editComment(const std::string& commentId,
                                                            const std::string& body,
                                                            const std::string& /*actorUserId*/,
                                                            const std::optional<std::int64_t> expectedVersion) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement current(database_, "SELECT version FROM comments WHERE id = ? AND deleted_at IS NULL");
        current.bind(1, commentId);
        if (current.step() != SQLITE_ROW) {
            executeScript("ROLLBACK;");
            return std::nullopt;
        }
        const std::int64_t currentVersion = sqlite3_column_int64(current.get(), 0);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Comment was modified by another user");
        }

        Statement update(database_, R"SQL(
UPDATE comments SET body = ?, version = version + 1, edited_at = CURRENT_TIMESTAMP, updated_at = CURRENT_TIMESTAMP
WHERE id = ?
)SQL");
        update.bind(1, body);
        update.bind(2, commentId);
        expectDone(database_, update, "Comment edit update");

        executeScript("COMMIT;");
        const std::string sql = std::string(CommentSelect) + "WHERE c.id = ?";
        Statement read(database_, sql);
        read.bind(1, commentId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Edited comment could not be read back");
        }
        return readComment(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SqliteDatabase::deleteComment(const std::string& commentId, const std::string& actorUserId) {
    std::scoped_lock lock(mutex_);
    const std::string actorId = requireUserId(database_, actorUserId);
    Statement statement(database_, R"SQL(
UPDATE comments SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = ?, updated_at = CURRENT_TIMESTAMP
WHERE id = ? AND deleted_at IS NULL
)SQL");
    statement.bind(1, actorId);
    statement.bind(2, commentId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

bool SqliteDatabase::addCommentReaction(const std::string& commentId, const std::string& userId,
                                        const std::string& reactionKey) {
    std::scoped_lock lock(mutex_);
    const std::string resolvedUserId = requireUserId(database_, userId);
    Statement statement(database_, R"SQL(
INSERT OR IGNORE INTO comment_reactions(comment_id, user_id, reaction_key) VALUES (?, ?, ?)
)SQL");
    statement.bind(1, commentId);
    statement.bind(2, resolvedUserId);
    statement.bind(3, reactionKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

bool SqliteDatabase::removeCommentReaction(const std::string& commentId, const std::string& userId,
                                           const std::string& reactionKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
DELETE FROM comment_reactions WHERE comment_id = ? AND user_id = ? AND reaction_key = ?
)SQL");
    statement.bind(1, commentId);
    statement.bind(2, userId);
    statement.bind(3, reactionKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::vector<Domain::CommentReaction> SqliteDatabase::listCommentReactions(const std::string& commentId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT r.reaction_key, u.id, u.display_name, u.email
FROM comment_reactions r JOIN users u ON u.id = r.user_id
WHERE r.comment_id = ?
ORDER BY r.reaction_key, u.display_name
)SQL");
    statement.bind(1, commentId);
    std::vector<Domain::CommentReaction> reactions;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        Domain::CommentReaction reaction;
        reaction.reactionKey = text(statement.get(), 0);
        reaction.user = readUserSummary(statement.get(), 1);
        reactions.push_back(std::move(reaction));
    }
    return reactions;
}

namespace {
Domain::Notification readNotification(sqlite3_stmt* statement) {
    Domain::Notification notification;
    notification.id = text(statement, 0);
    notification.type = text(statement, 1);
    notification.ticketKey = optionalText(statement, 2);
    notification.ticketSummary = optionalText(statement, 3);
    notification.readAt = optionalText(statement, 4);
    notification.createdAt = text(statement, 5);
    return notification;
}

constexpr const char* NotificationSelect = R"SQL(
SELECT n.id, n.type, i.ticket_key, i.summary, n.read_at, n.created_at
FROM notifications n
LEFT JOIN tickets i ON i.id = n.ticket_id
)SQL";
} // namespace

Domain::Notification SqliteDatabase::createNotification(const std::string& userId,
                                                         const std::string& type,
                                                         const std::string& ticketId) {
    std::scoped_lock lock(mutex_);
    const std::string notificationId = Common::uuidV4();
    Statement insert(database_, R"SQL(
INSERT INTO notifications(id, user_id, type, ticket_id, created_at) VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, notificationId);
    insert.bind(2, userId);
    insert.bind(3, type);
    insert.bind(4, ticketId);
    expectDone(database_, insert, "Insert notification");

    Statement read(database_, std::string(NotificationSelect) + "WHERE n.id = ?");
    read.bind(1, notificationId);
    if (read.step() != SQLITE_ROW) {
        throw std::runtime_error("Created notification could not be read back");
    }
    return readNotification(read.get());
}

std::vector<Domain::Notification> SqliteDatabase::listNotifications(const std::string& userId, const bool unreadOnly) {
    std::scoped_lock lock(mutex_);
    std::string sql = std::string(NotificationSelect) + "WHERE n.user_id = ?";
    if (unreadOnly) {
        sql += " AND n.read_at IS NULL";
    }
    sql += " ORDER BY n.created_at DESC";
    Statement statement(database_, sql);
    statement.bind(1, userId);
    std::vector<Domain::Notification> notifications;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        notifications.push_back(readNotification(statement.get()));
    }
    return notifications;
}

std::vector<Domain::Notification> SqliteDatabase::listNotifications(const std::string& userId, const bool unreadOnly,
                                                                      const int limit, const int offset) {
    std::scoped_lock lock(mutex_);
    std::string sql = std::string(NotificationSelect) + "WHERE n.user_id = ?1";
    if (unreadOnly) {
        sql += " AND n.read_at IS NULL";
    }
    sql += " ORDER BY n.created_at DESC LIMIT ?2 OFFSET ?3";
    Statement statement(database_, sql);
    statement.bind(1, userId);
    statement.bind(2, static_cast<std::int64_t>(limit));
    statement.bind(3, static_cast<std::int64_t>(offset));
    std::vector<Domain::Notification> notifications;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        notifications.push_back(readNotification(statement.get()));
    }
    return notifications;
}

std::int64_t SqliteDatabase::countNotifications(const std::string& userId, const bool unreadOnly) {
    std::scoped_lock lock(mutex_);
    std::string sql = "SELECT COUNT(*) FROM notifications WHERE user_id = ?1";
    if (unreadOnly) {
        sql += " AND read_at IS NULL";
    }
    Statement statement(database_, sql);
    statement.bind(1, userId);
    if (statement.step() != SQLITE_ROW) {
        return 0;
    }
    return sqlite3_column_int64(statement.get(), 0);
}

int SqliteDatabase::countUnreadNotifications(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "SELECT COUNT(*) FROM notifications WHERE user_id = ? AND read_at IS NULL");
    statement.bind(1, userId);
    if (statement.step() != SQLITE_ROW) {
        return 0;
    }
    return sqlite3_column_int(statement.get(), 0);
}

bool SqliteDatabase::markNotificationRead(const std::string& notificationId, const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE notifications SET read_at = CURRENT_TIMESTAMP WHERE id = ? AND user_id = ? AND read_at IS NULL
)SQL");
    statement.bind(1, notificationId);
    statement.bind(2, userId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

bool SqliteDatabase::markAllNotificationsRead(const std::string& userId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "UPDATE notifications SET read_at = CURRENT_TIMESTAMP WHERE user_id = ? AND read_at IS NULL");
    statement.bind(1, userId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::vector<Domain::Worklog> SqliteDatabase::listWorklogs(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(WorklogSelect) + R"SQL(
JOIN tickets i ON i.id = w.ticket_id
WHERE w.deleted_at IS NULL
  AND i.deleted_at IS NULL
  AND (i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1))
ORDER BY w.work_date DESC, w.created_at DESC
)SQL";
    Statement statement(database_, sql);
    statement.bind(1, ticketKey);
    std::vector<Domain::Worklog> worklogs;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        worklogs.push_back(readWorklog(statement.get()));
    }
    return worklogs;
}

Domain::Worklog SqliteDatabase::addWorklog(const Domain::AddWorklogRequest& request, const std::string& authorUserId) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, request.ticketKey);
    const std::string authorId = requireUserId(database_, authorUserId);
    const std::string worklogId = Common::uuidV4();

    Statement insert(database_, R"SQL(
INSERT INTO worklogs(id, ticket_id, author_user_id, work_date, time_spent_seconds, comment, created_at, updated_at)
VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, worklogId);
    insert.bind(2, ticketId);
    insert.bind(3, authorId);
    insert.bind(4, request.workDate);
    insert.bind(5, request.timeSpentSeconds);
    request.comment ? insert.bind(6, *request.comment) : insert.bindNull(6);
    expectDone(database_, insert, "Worklog insert");

    const std::string sql = std::string(WorklogSelect) + "WHERE w.id = ?";
    Statement read(database_, sql);
    read.bind(1, worklogId);
    if (read.step() != SQLITE_ROW) {
        throw std::runtime_error("Created worklog could not be read back");
    }
    return readWorklog(read.get());
}

std::optional<Domain::Worklog> SqliteDatabase::findWorklogById(const std::string& worklogId) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(WorklogSelect) + "WHERE w.id = ? AND w.deleted_at IS NULL";
    Statement statement(database_, sql);
    statement.bind(1, worklogId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readWorklog(statement.get());
}

std::optional<Domain::Worklog> SqliteDatabase::editWorklog(const std::string& worklogId,
                                                            const Domain::EditWorklogRequest& request,
                                                            const std::optional<std::int64_t> expectedVersion) {
    std::scoped_lock lock(mutex_);
    executeScript("BEGIN IMMEDIATE;");
    try {
        Statement current(database_, "SELECT version FROM worklogs WHERE id = ? AND deleted_at IS NULL");
        current.bind(1, worklogId);
        if (current.step() != SQLITE_ROW) {
            executeScript("ROLLBACK;");
            return std::nullopt;
        }
        const std::int64_t currentVersion = sqlite3_column_int64(current.get(), 0);
        if (expectedVersion && *expectedVersion != currentVersion) {
            throw Domain::ConcurrencyConflict("Worklog was modified by another user");
        }

        Statement update(database_, R"SQL(
UPDATE worklogs SET work_date = ?, time_spent_seconds = ?, comment = ?, version = version + 1, updated_at = CURRENT_TIMESTAMP
WHERE id = ?
)SQL");
        update.bind(1, request.workDate);
        update.bind(2, request.timeSpentSeconds);
        request.comment ? update.bind(3, *request.comment) : update.bindNull(3);
        update.bind(4, worklogId);
        expectDone(database_, update, "Worklog edit update");

        executeScript("COMMIT;");
        const std::string sql = std::string(WorklogSelect) + "WHERE w.id = ?";
        Statement read(database_, sql);
        read.bind(1, worklogId);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("Edited worklog could not be read back");
        }
        return readWorklog(read.get());
    } catch (...) {
        try {
            executeScript("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SqliteDatabase::deleteWorklog(const std::string& worklogId, const std::string& actorUserId) {
    std::scoped_lock lock(mutex_);
    const std::string actorId = requireUserId(database_, actorUserId);
    Statement statement(database_, R"SQL(
UPDATE worklogs SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = ?, updated_at = CURRENT_TIMESTAMP
WHERE id = ? AND deleted_at IS NULL
)SQL");
    statement.bind(1, actorId);
    statement.bind(2, worklogId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

void SqliteDatabase::recordAuditEvent(const std::string& category,
                                      const std::string& action,
                                      const std::optional<std::string> actorUserId,
                                      const std::optional<std::string> targetType,
                                      const std::optional<std::string> targetId,
                                      const std::optional<std::string> details) {
    std::scoped_lock lock(mutex_);
    const std::string eventId = Common::uuidV4();
    Statement insert(database_, R"SQL(
INSERT INTO audit_events(id, category, action, actor_user_id, target_type, target_id, details, created_at)
VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, eventId);
    insert.bind(2, category);
    insert.bind(3, action);
    actorUserId ? insert.bind(4, *actorUserId) : insert.bindNull(4);
    targetType ? insert.bind(5, *targetType) : insert.bindNull(5);
    targetId ? insert.bind(6, *targetId) : insert.bindNull(6);
    details ? insert.bind(7, *details) : insert.bindNull(7);
    expectDone(database_, insert, "Audit event insert");
}

std::vector<Domain::AuditEvent> SqliteDatabase::listAuditEvents(const int limit) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(AuditEventSelect) + "ORDER BY a.created_at DESC LIMIT ?");
    statement.bind(1, static_cast<std::int64_t>(limit));
    std::vector<Domain::AuditEvent> events;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        events.push_back(readAuditEvent(statement.get()));
    }
    return events;
}

std::vector<Domain::AuditEvent> SqliteDatabase::listAuditEvents(const int limit, const int offset) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(AuditEventSelect) + "ORDER BY a.created_at DESC LIMIT ?1 OFFSET ?2");
    statement.bind(1, static_cast<std::int64_t>(limit));
    statement.bind(2, static_cast<std::int64_t>(offset));
    std::vector<Domain::AuditEvent> events;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        events.push_back(readAuditEvent(statement.get()));
    }
    return events;
}

std::int64_t SqliteDatabase::countAuditEvents() {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "SELECT COUNT(*) FROM audit_events");
    if (statement.step() != SQLITE_ROW) {
        return 0;
    }
    return sqlite3_column_int64(statement.get(), 0);
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
FROM tickets i JOIN ticket_statuses s ON s.id = i.status_id
WHERE i.deleted_at IS NULL
)SQL");
        if (statement.step() == SQLITE_ROW) {
            stats.totalTickets = sqlite3_column_int64(statement.get(), 0);
            stats.todoTickets = sqlite3_column_int64(statement.get(), 1);
            stats.inProgressTickets = sqlite3_column_int64(statement.get(), 2);
            stats.doneTickets = sqlite3_column_int64(statement.get(), 3);
        }
    }
    auto recent = listTickets({});
    if (recent.size() > 8) {
        recent.resize(8);
    }
    stats.recentTickets = std::move(recent);
    return stats;
}

std::vector<Domain::BoardColumn> SqliteDatabase::listBoardColumns() {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, BoardColumnSelect);
    std::vector<Domain::BoardColumn> columns;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        columns.push_back(readBoardColumn(statement.get()));
    }
    return columns;
}

bool SqliteDatabase::setBoardColumnWipLimit(const std::string& statusKey, const std::optional<int> wipLimit) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE board_columns SET wip_limit = ?1
WHERE status_id = (SELECT id FROM ticket_statuses WHERE status_key = ?2)
)SQL");
    wipLimit ? statement.bind(1, static_cast<std::int64_t>(*wipLimit)) : statement.bindNull(1);
    statement.bind(2, statusKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

Domain::TicketLink SqliteDatabase::createTicketLink(const std::string& sourceTicketKey,
                                                  const std::string& targetTicketKey,
                                                  const std::string& linkType) {
    std::scoped_lock lock(mutex_);
    const std::string sourceId = lookupTicketId(database_, sourceTicketKey);
    const std::string targetId = lookupTicketId(database_, targetTicketKey);

    Statement duplicate(database_,
        "SELECT 1 FROM ticket_links WHERE source_ticket_id = ? AND target_ticket_id = ? AND link_type = ?");
    duplicate.bind(1, sourceId);
    duplicate.bind(2, targetId);
    duplicate.bind(3, linkType);
    if (duplicate.step() == SQLITE_ROW) {
        throw std::invalid_argument("That link already exists");
    }

    const std::string linkId = Common::uuidV4();
    Statement insert(database_, R"SQL(
INSERT INTO ticket_links(id, source_ticket_id, target_ticket_id, link_type) VALUES (?, ?, ?, ?)
)SQL");
    insert.bind(1, linkId);
    insert.bind(2, sourceId);
    insert.bind(3, targetId);
    insert.bind(4, linkType);
    expectDone(database_, insert, "Insert ticket link");

    Domain::TicketLink link;
    link.id = linkId;
    link.linkType = linkType;
    link.outward = true;

    Statement targetRow(database_, "SELECT ticket_key, summary FROM tickets WHERE id = ?");
    targetRow.bind(1, targetId);
    if (targetRow.step() == SQLITE_ROW) {
        link.otherTicketKey = text(targetRow.get(), 0);
        link.otherTicketSummary = text(targetRow.get(), 1);
    }
    return link;
}

std::vector<Domain::TicketLink> SqliteDatabase::listTicketLinks(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);

    Statement statement(database_, R"SQL(
SELECT l.id, l.link_type, 1 AS outward, tgt.ticket_key, tgt.summary
FROM ticket_links l JOIN tickets tgt ON tgt.id = l.target_ticket_id
WHERE l.source_ticket_id = ?1 AND tgt.deleted_at IS NULL
UNION ALL
SELECT l.id, l.link_type, 0 AS outward, src.ticket_key, src.summary
FROM ticket_links l JOIN tickets src ON src.id = l.source_ticket_id
WHERE l.target_ticket_id = ?1 AND src.deleted_at IS NULL
)SQL");
    statement.bind(1, ticketId);
    std::vector<Domain::TicketLink> links;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        Domain::TicketLink link;
        link.id = text(statement.get(), 0);
        link.linkType = text(statement.get(), 1);
        link.outward = sqlite3_column_int(statement.get(), 2) != 0;
        link.otherTicketKey = text(statement.get(), 3);
        link.otherTicketSummary = text(statement.get(), 4);
        links.push_back(std::move(link));
    }
    return links;
}

std::optional<Domain::TicketLinkDetail> SqliteDatabase::findTicketLinkById(const std::string& linkId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT l.id, l.link_type, src.ticket_key, srcProj.project_key, tgt.ticket_key, tgtProj.project_key
FROM ticket_links l
JOIN tickets src ON src.id = l.source_ticket_id
JOIN projects srcProj ON srcProj.id = src.project_id
JOIN tickets tgt ON tgt.id = l.target_ticket_id
JOIN projects tgtProj ON tgtProj.id = tgt.project_id
WHERE l.id = ?
)SQL");
    statement.bind(1, linkId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    Domain::TicketLinkDetail detail;
    detail.id = text(statement.get(), 0);
    detail.linkType = text(statement.get(), 1);
    detail.sourceTicketKey = text(statement.get(), 2);
    detail.sourceProjectKey = text(statement.get(), 3);
    detail.targetTicketKey = text(statement.get(), 4);
    detail.targetProjectKey = text(statement.get(), 5);
    return detail;
}

bool SqliteDatabase::deleteTicketLink(const std::string& linkId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "DELETE FROM ticket_links WHERE id = ?");
    statement.bind(1, linkId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

namespace {
// Shared by the two structurally-identical watch/vote tables (ticket_watchers,
// ticket_votes: both (ticket_id, user_id) composite-PK many-to-many tables with
// no other columns worth reading back). `table` is always a fixed internal
// literal, never caller input, matching the existing `lookupId` pattern.
bool insertMembership(sqlite3* database, const char* table, const std::string& ticketId, const std::string& userId) {
    Statement statement(database, std::string("INSERT OR IGNORE INTO ") + table + "(ticket_id, user_id) VALUES (?, ?)");
    statement.bind(1, ticketId);
    statement.bind(2, userId);
    statement.step();
    return sqlite3_changes(database) > 0;
}

bool deleteMembership(sqlite3* database, const char* table, const std::string& ticketId, const std::string& userId) {
    Statement statement(database, std::string("DELETE FROM ") + table + " WHERE ticket_id = ? AND user_id = ?");
    statement.bind(1, ticketId);
    statement.bind(2, userId);
    statement.step();
    return sqlite3_changes(database) > 0;
}

std::vector<Domain::UserSummary> listMembers(sqlite3* database, const char* table, const std::string& ticketId) {
    Statement statement(database, std::string("SELECT u.id, u.display_name, u.email FROM ") + table
        + " t JOIN users u ON u.id = t.user_id WHERE t.ticket_id = ? ORDER BY u.display_name");
    statement.bind(1, ticketId);
    std::vector<Domain::UserSummary> users;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        users.push_back(readUserSummary(statement.get(), 0));
    }
    return users;
}
} // namespace

bool SqliteDatabase::watchTicket(const std::string& ticketKey, const std::string& userId) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);
    const std::string resolvedUserId = requireUserId(database_, userId);
    return insertMembership(database_, "ticket_watchers", ticketId, resolvedUserId);
}

bool SqliteDatabase::unwatchTicket(const std::string& ticketKey, const std::string& userId) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);
    return deleteMembership(database_, "ticket_watchers", ticketId, userId);
}

std::vector<Domain::UserSummary> SqliteDatabase::listWatchers(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);
    return listMembers(database_, "ticket_watchers", ticketId);
}

std::vector<Domain::Ticket> SqliteDatabase::listWatchedTickets(const std::string& userId, int limit) {
    std::scoped_lock lock(mutex_);
    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NULL
  AND p.deleted_at IS NULL
  AND EXISTS (SELECT 1 FROM ticket_watchers w WHERE w.ticket_id = i.id AND w.user_id = ?1)
GROUP BY i.id
ORDER BY i.updated_at DESC
LIMIT ?2
)SQL";
    Statement statement(database_, sql);
    statement.bind(1, userId);
    statement.bind(2, static_cast<std::int64_t>(limit));
    std::vector<Domain::Ticket> tickets;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        tickets.push_back(readTicket(statement.get()));
    }
    return tickets;
}

bool SqliteDatabase::voteTicket(const std::string& ticketKey, const std::string& userId) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);
    const std::string resolvedUserId = requireUserId(database_, userId);
    return insertMembership(database_, "ticket_votes", ticketId, resolvedUserId);
}

bool SqliteDatabase::unvoteTicket(const std::string& ticketKey, const std::string& userId) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);
    return deleteMembership(database_, "ticket_votes", ticketId, userId);
}

std::vector<Domain::UserSummary> SqliteDatabase::listVoters(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);
    return listMembers(database_, "ticket_votes", ticketId);
}

bool SqliteDatabase::softDeleteTicket(const std::string& ticketKey, const std::string& actorUserId) {
    std::scoped_lock lock(mutex_);
    const std::string actorId = requireUserId(database_, actorUserId);
    Statement statement(database_, R"SQL(
UPDATE tickets SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = ?, updated_at = CURRENT_TIMESTAMP
WHERE (ticket_key = ?2 OR id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?2)) AND deleted_at IS NULL
)SQL");
    statement.bind(1, actorId);
    statement.bind(2, ticketKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

bool SqliteDatabase::restoreTicket(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE tickets SET deleted_at = NULL, deleted_by_user_id = NULL, updated_at = CURRENT_TIMESTAMP
WHERE ticket_key = ? AND deleted_at IS NOT NULL
)SQL");
    statement.bind(1, ticketKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::vector<Domain::Ticket> SqliteDatabase::listDeletedTickets() {
    std::scoped_lock lock(mutex_);
    // Fixed 90-day retention, checked on demand -- there is no background
    // job to purge proactively (D89-analog for tickets, D51).
    executeScript("DELETE FROM tickets WHERE deleted_at IS NOT NULL AND deleted_at <= datetime('now', '-90 days');");

    const std::string sql = std::string(TicketSelect) + R"SQL(
WHERE i.deleted_at IS NOT NULL
GROUP BY i.id
ORDER BY i.deleted_at DESC
)SQL";
    Statement statement(database_, sql);
    std::vector<Domain::Ticket> tickets;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        tickets.push_back(readTicket(statement.get()));
    }
    return tickets;
}

bool SqliteDatabase::permanentlyDeleteTicket(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "DELETE FROM tickets WHERE ticket_key = ? AND deleted_at IS NOT NULL");
    statement.bind(1, ticketKey);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

Domain::Attachment SqliteDatabase::createAttachment(const std::string& id,
                                                    const std::string& ticketKey,
                                                    const std::string& uploaderUserId,
                                                    const std::string& fileName,
                                                    const std::string& contentType,
                                                    const std::int64_t byteSize,
                                                    const std::string& sha256) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);
    const std::string uploaderId = requireUserId(database_, uploaderUserId);
    Statement insert(database_, R"SQL(
INSERT INTO attachments(id, ticket_id, uploader_user_id, file_name, content_type, byte_size, storage_key, sha256)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?1, ?7)
)SQL");
    insert.bind(1, id);
    insert.bind(2, ticketId);
    insert.bind(3, uploaderId);
    insert.bind(4, fileName);
    insert.bind(5, contentType);
    insert.bind(6, byteSize);
    insert.bind(7, sha256);
    insert.step();

    Statement read(database_, std::string(AttachmentSelect) + "WHERE a.id = ?");
    read.bind(1, id);
    if (read.step() != SQLITE_ROW) {
        throw std::runtime_error("Failed to read back created attachment");
    }
    return readAttachment(read.get());
}

std::vector<Domain::Attachment> SqliteDatabase::listAttachments(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    const std::string ticketId = lookupTicketId(database_, ticketKey);
    Statement statement(database_, std::string(AttachmentSelect) + "WHERE a.ticket_id = ?1 AND a.deleted_at IS NULL ORDER BY a.created_at");
    statement.bind(1, ticketId);
    std::vector<Domain::Attachment> attachments;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        attachments.push_back(readAttachment(statement.get()));
    }
    return attachments;
}

std::optional<Domain::Attachment> SqliteDatabase::findAttachmentById(const std::string& attachmentId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(AttachmentSelect) + "WHERE a.id = ?");
    statement.bind(1, attachmentId);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readAttachment(statement.get());
}

bool SqliteDatabase::softDeleteAttachment(const std::string& attachmentId, const std::string& actorUserId) {
    std::scoped_lock lock(mutex_);
    const std::string actorId = requireUserId(database_, actorUserId);
    Statement statement(database_, R"SQL(
UPDATE attachments SET deleted_at = CURRENT_TIMESTAMP, deleted_by_user_id = ?
WHERE id = ?2 AND deleted_at IS NULL
)SQL");
    statement.bind(1, actorId);
    statement.bind(2, attachmentId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

bool SqliteDatabase::restoreAttachment(const std::string& attachmentId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
UPDATE attachments SET deleted_at = NULL, deleted_by_user_id = NULL
WHERE id = ? AND deleted_at IS NOT NULL
)SQL");
    statement.bind(1, attachmentId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::vector<Domain::Attachment> SqliteDatabase::listDeletedAttachments() {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(AttachmentSelect) + "WHERE a.deleted_at IS NOT NULL ORDER BY a.deleted_at DESC");
    std::vector<Domain::Attachment> attachments;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        attachments.push_back(readAttachment(statement.get()));
    }
    return attachments;
}

bool SqliteDatabase::permanentlyDeleteAttachment(const std::string& attachmentId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "DELETE FROM attachments WHERE id = ? AND deleted_at IS NOT NULL");
    statement.bind(1, attachmentId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

std::vector<std::string> SqliteDatabase::listAttachmentStorageKeysForTicket(const std::string& ticketKey) {
    std::scoped_lock lock(mutex_);
    // Deliberately does not use lookupTicketId (which excludes soft-deleted
    // tickets): this is called right before a permanent delete, at which
    // point the ticket is expected to already be soft-deleted.
    Statement statement(database_, R"SQL(
SELECT a.storage_key FROM attachments a JOIN tickets i ON i.id = a.ticket_id
WHERE i.ticket_key = ?1 OR i.id = (SELECT ticket_id FROM ticket_key_aliases WHERE alias_key = ?1)
)SQL");
    statement.bind(1, ticketKey);
    std::vector<std::string> keys;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        keys.push_back(text(statement.get(), 0));
    }
    return keys;
}

std::vector<std::string> SqliteDatabase::listAttachmentStorageKeysForProject(const std::string& projectKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT a.storage_key FROM attachments a
JOIN tickets i ON i.id = a.ticket_id
JOIN projects p ON p.id = i.project_id
WHERE p.project_key = ?
)SQL");
    statement.bind(1, projectKey);
    std::vector<std::string> keys;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        keys.push_back(text(statement.get(), 0));
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
       u.id, u.display_name, u.email, w.created_at
FROM webhook_subscriptions w
LEFT JOIN users u ON u.id = w.created_by_user_id
)SQL";

Domain::WebhookSubscription readWebhookSubscription(sqlite3_stmt* statement) {
    Domain::WebhookSubscription subscription;
    subscription.id = text(statement, 0);
    subscription.targetUrl = text(statement, 1);
    subscription.secret = text(statement, 2);
    subscription.eventTypes = splitLabels(text(statement, 3));
    subscription.projectKey = optionalText(statement, 4);
    subscription.enabled = boolColumn(statement, 5);
    if (sqlite3_column_type(statement, 6) != SQLITE_NULL) {
        subscription.createdBy = readUserSummary(statement, 6);
    }
    subscription.createdAt = text(statement, 9);
    return subscription;
}
} // namespace

std::vector<Domain::WebhookSubscription> SqliteDatabase::listWebhookSubscriptions() {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, std::string(WebhookSubscriptionSelect) + "ORDER BY w.created_at DESC");
    std::vector<Domain::WebhookSubscription> subscriptions;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        subscriptions.push_back(readWebhookSubscription(statement.get()));
    }
    return subscriptions;
}

Domain::WebhookSubscription SqliteDatabase::createWebhookSubscription(
    const Domain::CreateWebhookSubscriptionRequest& request, const std::string& createdByUserId) {
    std::scoped_lock lock(mutex_);
    const std::string subscriptionId = Common::uuidV4();
    // The signing secret is generated here (not supplied by the caller) and
    // stored in cleartext -- unlike a session/PAT token (a bearer
    // credential verified by hash comparison), this is an HMAC signing key
    // the CLI must re-read at delivery time to sign each payload, so it
    // cannot be stored as a one-way hash.
    const std::string secret = Common::randomTokenHex(32);
    Statement insert(database_, R"SQL(
INSERT INTO webhook_subscriptions(id, target_url, secret, event_types, project_key, created_by_user_id, created_at)
VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, subscriptionId);
    insert.bind(2, request.targetUrl);
    insert.bind(3, secret);
    insert.bind(4, joinComma(request.eventTypes));
    request.projectKey ? insert.bind(5, *request.projectKey) : insert.bindNull(5);
    insert.bind(6, createdByUserId);
    expectDone(database_, insert, "Webhook subscription insert");

    Statement read(database_, std::string(WebhookSubscriptionSelect) + "WHERE w.id = ?");
    read.bind(1, subscriptionId);
    if (read.step() != SQLITE_ROW) {
        throw std::runtime_error("Created webhook subscription could not be read back");
    }
    return readWebhookSubscription(read.get());
}

bool SqliteDatabase::deleteWebhookSubscription(const std::string& subscriptionId) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, "DELETE FROM webhook_subscriptions WHERE id = ?");
    statement.bind(1, subscriptionId);
    statement.step();
    return sqlite3_changes(database_) > 0;
}

void SqliteDatabase::createWebhookDelivery(const std::string& subscriptionId, const std::string& eventType,
                                            const std::string& payload) {
    std::scoped_lock lock(mutex_);
    Statement insert(database_, R"SQL(
INSERT INTO webhook_deliveries(id, subscription_id, event_type, payload, created_at, next_attempt_at)
VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, Common::uuidV4());
    insert.bind(2, subscriptionId);
    insert.bind(3, eventType);
    insert.bind(4, payload);
    expectDone(database_, insert, "Webhook delivery insert");
}

std::vector<Domain::WebhookDelivery> SqliteDatabase::listPendingWebhookDeliveries(const int limit) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT d.id, d.subscription_id, w.target_url, w.secret, d.event_type, d.payload, d.attempt_count
FROM webhook_deliveries d
JOIN webhook_subscriptions w ON w.id = d.subscription_id
WHERE d.status = 'pending' AND d.next_attempt_at <= CURRENT_TIMESTAMP
ORDER BY d.created_at
LIMIT ?
)SQL");
    statement.bind(1, static_cast<std::int64_t>(limit));
    std::vector<Domain::WebhookDelivery> deliveries;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        Domain::WebhookDelivery delivery;
        delivery.id = text(statement.get(), 0);
        delivery.subscriptionId = text(statement.get(), 1);
        delivery.targetUrl = text(statement.get(), 2);
        delivery.secret = text(statement.get(), 3);
        delivery.eventType = text(statement.get(), 4);
        delivery.payload = text(statement.get(), 5);
        delivery.attemptCount = sqlite3_column_int(statement.get(), 6);
        deliveries.push_back(std::move(delivery));
    }
    return deliveries;
}

void SqliteDatabase::recordWebhookDeliveryResult(const std::string& deliveryId, const bool success,
                                                  const std::optional<std::string>& error) {
    std::scoped_lock lock(mutex_);
    if (success) {
        Statement update(database_, R"SQL(
UPDATE webhook_deliveries SET status = 'delivered', delivered_at = CURRENT_TIMESTAMP, last_error = NULL
WHERE id = ?
)SQL");
        update.bind(1, deliveryId);
        expectDone(database_, update, "Record webhook delivery success");
        return;
    }
    // A failed attempt either gets rescheduled or, past
    // Domain::MaxDeliveryAttempts, permanently marked "failed" -- computed
    // in SQL against the row's own just-incremented attempt_count so this
    // stays a single statement (no read-modify-write race with a
    // concurrent process-outbox run, though in practice this CLI command is
    // not expected to run concurrently with itself).
    Statement update(database_, R"SQL(
UPDATE webhook_deliveries
SET attempt_count = attempt_count + 1,
    last_error = ?2,
    status = CASE WHEN attempt_count + 1 >= ?3 THEN 'failed' ELSE status END,
    next_attempt_at = datetime(CURRENT_TIMESTAMP, '+' || ?4 || ' minutes')
WHERE id = ?1
)SQL");
    update.bind(1, deliveryId);
    error ? update.bind(2, *error) : update.bindNull(2);
    update.bind(3, static_cast<std::int64_t>(Domain::MaxDeliveryAttempts));
    update.bind(4, static_cast<std::int64_t>(Domain::DeliveryRetryDelayMinutes));
    expectDone(database_, update, "Record webhook delivery failure");
}

void SqliteDatabase::createEmailDelivery(const std::string& recipientUserId, const std::string& subject,
                                          const std::string& body) {
    std::scoped_lock lock(mutex_);
    Statement insert(database_, R"SQL(
INSERT INTO email_deliveries(id, recipient_user_id, subject, body, created_at, next_attempt_at)
VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, Common::uuidV4());
    insert.bind(2, recipientUserId);
    insert.bind(3, subject);
    insert.bind(4, body);
    expectDone(database_, insert, "Email delivery insert");
}

std::vector<Domain::EmailDelivery> SqliteDatabase::listPendingEmailDeliveries(const int limit) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT d.id, u.email, d.subject, d.body, d.attempt_count
FROM email_deliveries d
JOIN users u ON u.id = d.recipient_user_id
WHERE d.status = 'pending' AND d.next_attempt_at <= CURRENT_TIMESTAMP
ORDER BY d.created_at
LIMIT ?
)SQL");
    statement.bind(1, static_cast<std::int64_t>(limit));
    std::vector<Domain::EmailDelivery> deliveries;
    for (int result = statement.step(); result == SQLITE_ROW; result = statement.step()) {
        Domain::EmailDelivery delivery;
        delivery.id = text(statement.get(), 0);
        delivery.recipientEmail = text(statement.get(), 1);
        delivery.subject = text(statement.get(), 2);
        delivery.body = text(statement.get(), 3);
        delivery.attemptCount = sqlite3_column_int(statement.get(), 4);
        deliveries.push_back(std::move(delivery));
    }
    return deliveries;
}

void SqliteDatabase::recordEmailDeliveryResult(const std::string& deliveryId, const bool success,
                                                const std::optional<std::string>& error) {
    std::scoped_lock lock(mutex_);
    if (success) {
        Statement update(database_, R"SQL(
UPDATE email_deliveries SET status = 'delivered', sent_at = CURRENT_TIMESTAMP, last_error = NULL WHERE id = ?
)SQL");
        update.bind(1, deliveryId);
        expectDone(database_, update, "Record email delivery success");
        return;
    }
    Statement update(database_, R"SQL(
UPDATE email_deliveries
SET attempt_count = attempt_count + 1,
    last_error = ?2,
    status = CASE WHEN attempt_count + 1 >= ?3 THEN 'failed' ELSE status END,
    next_attempt_at = datetime(CURRENT_TIMESTAMP, '+' || ?4 || ' minutes')
WHERE id = ?1
)SQL");
    update.bind(1, deliveryId);
    error ? update.bind(2, *error) : update.bindNull(2);
    update.bind(3, static_cast<std::int64_t>(Domain::MaxDeliveryAttempts));
    update.bind(4, static_cast<std::int64_t>(Domain::DeliveryRetryDelayMinutes));
    expectDone(database_, update, "Record email delivery failure");
}

std::optional<Domain::IdempotencyRecord> SqliteDatabase::findIdempotencyRecord(
    const std::string& userId, const std::string& idempotencyKey) {
    std::scoped_lock lock(mutex_);
    Statement statement(database_, R"SQL(
SELECT request_hash, response_status, response_body FROM idempotency_keys
WHERE user_id = ? AND idempotency_key = ?
)SQL");
    statement.bind(1, userId);
    statement.bind(2, idempotencyKey);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    Domain::IdempotencyRecord record;
    record.requestHash = text(statement.get(), 0);
    record.responseStatus = sqlite3_column_int(statement.get(), 1);
    record.responseBody = text(statement.get(), 2);
    return record;
}

void SqliteDatabase::recordIdempotencyResult(const std::string& userId, const std::string& idempotencyKey,
                                             const std::string& requestHash, const int responseStatus,
                                             const std::string& responseBody) {
    std::scoped_lock lock(mutex_);
    // Best-effort: INSERT OR IGNORE rather than a plain INSERT, so a narrow
    // concurrent-retry race (two requests carrying the same key arriving
    // before either has stored its result) never surfaces as a 500 -- the
    // caller's own response has already been computed and returned either
    // way, this write is only about caching it for a *future* retry.
    Statement insert(database_, R"SQL(
INSERT OR IGNORE INTO idempotency_keys(user_id, idempotency_key, request_hash, response_status, response_body, created_at)
VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
)SQL");
    insert.bind(1, userId);
    insert.bind(2, idempotencyKey);
    insert.bind(3, requestHash);
    insert.bind(4, static_cast<std::int64_t>(responseStatus));
    insert.bind(5, responseBody);
    expectDone(database_, insert, "Record idempotency result");
}

} // namespace TicketHub::Infrastructure::Database
