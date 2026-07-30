#pragma once

#include <cstdint>
#include <string>

namespace TicketHub::Config {

struct AppConfig {
    std::string databaseDriver{"postgres"};
    std::string databaseUrl{"host=127.0.0.1 port=5432 dbname=tickethub user=tickethub"};
    std::string sqlitePath{"./ticket-hub.db"};
    std::string bindAddress{"127.0.0.1"};
    std::uint16_t port{8080};
    bool autoMigrate{true};
    bool seedDemo{true};
    std::string webRoot;
    std::string migrationsRoot;

    static AppConfig fromEnvironment();
};

} // namespace TicketHub::Config
