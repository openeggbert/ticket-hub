#include "config/Config.h"
#include "infrastructure/database/DatabaseFactory.h"

#include <exception>
#include <iostream>
#include <string>

#ifndef TICKETHUB_VERSION
#define TICKETHUB_VERSION "development"
#endif

namespace {

void printUsage(const char* executable) {
    std::cout
        << "Ticket Hub CLI " << TICKETHUB_VERSION << "\n\n"
        << "Usage: " << executable << " <command>\n\n"
        << "Commands:\n"
        << "  migrate       Apply all pending schema migrations.\n"
        << "  seed-demo     Apply migrations and insert idempotent demo data.\n"
        << "  diagnostics   Print resolved non-secret configuration.\n"
        << "  version       Print the Ticket Hub version.\n";
}

void printDiagnostics(const TicketHub::Config::AppConfig& config) {
    std::cout << "version=" << TICKETHUB_VERSION << '\n'
              << "database_driver=" << config.databaseDriver << '\n'
              << "sqlite_path=" << config.sqlitePath << '\n'
              << "bind_address=" << config.bindAddress << '\n'
              << "port=" << config.port << '\n'
              << "auto_migrate=" << (config.autoMigrate ? "true" : "false") << '\n'
              << "seed_demo=" << (config.seedDemo ? "true" : "false") << '\n'
              << "web_root=" << config.webRoot << '\n'
              << "migrations_root=" << config.migrationsRoot << '\n';

    if (config.databaseDriver == "sqlite") {
        std::cout << "deployment_mode=single-process\n";
    } else {
        std::cout << "database_url=<redacted>\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        printUsage(argv[0]);
        return argc == 1 ? 0 : 2;
    }

    const std::string command(argv[1]);
    if (command == "version" || command == "--version") {
        std::cout << TICKETHUB_VERSION << '\n';
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        printUsage(argv[0]);
        return 0;
    }

    try {
        const auto config = TicketHub::Config::AppConfig::fromEnvironment();
        if (command == "diagnostics") {
            printDiagnostics(config);
            return 0;
        }

        auto database = TicketHub::Infrastructure::Database::createDatabase(config);
        if (command == "migrate") {
            database->migrate();
            std::cout << "Applied Ticket Hub migrations using " << database->backendName() << ".\n";
            return 0;
        }
        if (command == "seed-demo") {
            database->migrate();
            database->seedDemoData();
            std::cout << "Applied migrations and demo seed using " << database->backendName() << ".\n";
            return 0;
        }

        std::cerr << "Unknown command: " << command << "\n\n";
        printUsage(argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Ticket Hub CLI failed: " << error.what() << '\n';
        return 1;
    }
}
