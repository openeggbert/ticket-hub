#include "infrastructure/database/DatabaseFactory.h"

#ifdef TICKETHUB_WITH_POSTGRES
#include "infrastructure/database/PostgresDatabase.h"
#endif
#ifdef TICKETHUB_WITH_SQLITE
#include "infrastructure/database/SqliteDatabase.h"
#endif

#include <stdexcept>

namespace TicketHub::Infrastructure::Database {

std::shared_ptr<IDatabase> createDatabase(const Config::AppConfig& config) {
    if (config.databaseDriver == "postgres" || config.databaseDriver == "postgresql") {
#ifdef TICKETHUB_WITH_POSTGRES
        return std::make_shared<PostgresDatabase>(
            config.databaseUrl,
            config.migrationsRoot + "/postgresql",
            config.migrationsRoot + "/postgresql/002_seed_demo.sql");
#else
        throw std::runtime_error("PostgreSQL support was disabled at build time");
#endif
    }

    if (config.databaseDriver == "sqlite") {
#ifdef TICKETHUB_WITH_SQLITE
        return std::make_shared<SqliteDatabase>(
            config.sqlitePath,
            config.migrationsRoot + "/sqlite",
            config.migrationsRoot + "/sqlite/002_seed_demo.sql");
#else
        throw std::runtime_error("SQLite support was disabled at build time");
#endif
    }

    throw std::invalid_argument("Unsupported database driver: " + config.databaseDriver);
}

} // namespace TicketHub::Infrastructure::Database
