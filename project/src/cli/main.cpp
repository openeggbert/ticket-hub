#include "application/AuthService.h"
#include "config/Config.h"
#include "infrastructure/database/DatabaseFactory.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>

#ifndef TICKETHUB_VERSION
#define TICKETHUB_VERSION "development"
#endif

namespace {

void printUsage(const char* executable) {
    std::cout
        << "Ticket Hub CLI " << TICKETHUB_VERSION << "\n\n"
        << "Usage: " << executable << " <command> [arguments]\n\n"
        << "Commands:\n"
        << "  migrate                                     Apply all pending schema migrations.\n"
        << "  seed-demo                                    Apply migrations and insert idempotent demo data.\n"
        << "  create-user <email> <displayName> <password> [--admin] [--handle=<handle>]\n"
        << "                                                Create a local account directly (no invitation\n"
        << "                                                flow, no forced password change). This is the\n"
        << "                                                entire registration story for V1. --handle sets\n"
        << "                                                the optional, unique @mention handle (D56/D80).\n"
        << "  diagnostics                                  Print resolved non-secret configuration.\n"
        << "  version                                      Print the Ticket Hub version.\n";
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

int runCreateUser(const TicketHub::Config::AppConfig& config, int argc, char** argv) {
    if (argc < 5 || argc > 7) {
        std::cerr << "Usage: create-user <email> <displayName> <password> [--admin] [--handle=<handle>]\n";
        return 2;
    }
    TicketHub::Domain::CreateUserRequest request;
    request.email = argv[2];
    request.displayName = argv[3];
    request.password = argv[4];
    for (int index = 5; index < argc; ++index) {
        const std::string flag = argv[index];
        if (flag == "--admin") {
            request.isAdmin = true;
        } else if (flag.rfind("--handle=", 0) == 0) {
            request.handle = flag.substr(std::string("--handle=").length());
        } else {
            std::cerr << "Usage: create-user <email> <displayName> <password> [--admin] [--handle=<handle>]\n";
            return 2;
        }
    }

    auto database = TicketHub::Infrastructure::Database::createDatabase(config);
    TicketHub::Application::AuthService authService(database);
    const auto user = authService.createUser(std::move(request));
    // Never log the password. The id/email/handle/isAdmin confirmation below
    // is deliberately the only feedback given.
    std::cout << "Created user " << user.email << " (id=" << user.id
              << ", handle=" << (user.handle ? *user.handle : "none")
              << ", admin=" << (user.isAdmin ? "true" : "false") << ")\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 0;
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
            if (argc != 2) {
                printUsage(argv[0]);
                return 2;
            }
            printDiagnostics(config);
            return 0;
        }

        if (command == "create-user") {
            return runCreateUser(config, argc, argv);
        }

        if (argc != 2) {
            printUsage(argv[0]);
            return 2;
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
