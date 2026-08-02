#include "application/AuthService.h"
#include "config/Config.h"
#include "infrastructure/database/DatabaseFactory.h"

#include <exception>
#include <filesystem>
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
        << "  backup <output-directory>                    Back up the database and attachments directory\n"
        << "                                                into <output-directory> (must not already exist\n"
        << "                                                or must be empty). Offline/maintenance-window\n"
        << "                                                use only -- stop the server first (D106/D107).\n"
        << "  restore <backup-directory> --yes             Restore the database and attachments directory\n"
        << "                                                from <backup-directory>, overwriting the current\n"
        << "                                                ones. Destructive and irreversible; requires\n"
        << "                                                --yes. Runs pending migrations afterward. Stop\n"
        << "                                                the server first (D108).\n"
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
              << "migrations_root=" << config.migrationsRoot << '\n'
              << "attachments_root=" << config.attachmentsRoot << '\n';

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

// Backup (D106/D107): copies the attachments directory and asks the
// database adapter to dump itself into the same output directory. Offline/
// maintenance-window use only -- this command does not check whether the
// server is currently running against the same database/attachments
// directory, matching D107's "no online consistent snapshot logic" scope;
// the admin is responsible for stopping the server first.
int runBackup(const TicketHub::Config::AppConfig& config, int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: backup <output-directory>\n";
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path outputDirectory(argv[2]);
    if (fs::exists(outputDirectory) && !fs::is_empty(outputDirectory)) {
        std::cerr << "Refusing to back up into a non-empty directory: " << outputDirectory.string() << "\n";
        return 2;
    }
    fs::create_directories(outputDirectory);

    const fs::path attachmentsSource(config.attachmentsRoot);
    if (fs::exists(attachmentsSource)) {
        fs::copy(attachmentsSource, outputDirectory / "attachments",
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks);
    }

    auto database = TicketHub::Infrastructure::Database::createDatabase(config);
    database->backup(outputDirectory.string());

    std::cout << "Backup complete: " << outputDirectory.string() << " (" << database->backendName() << ")\n";
    return 0;
}

// Restore (D108): direct restore into the target database and attachments
// directory, with a mandatory --yes confirmation flag since this
// permanently overwrites current data. No isolated staging environment; the
// admin is responsible for their own pre-restore backup of the data being
// replaced. Runs pending migrations afterward as a separate, visible step
// (D109: forward-migrate an older backup to the current schema).
int runRestore(const TicketHub::Config::AppConfig& config, int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: restore <backup-directory> --yes\n";
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path backupDirectory(argv[2]);
    bool confirmed = false;
    for (int index = 3; index < argc; ++index) {
        if (std::string(argv[index]) == "--yes") {
            confirmed = true;
        }
    }
    if (!confirmed) {
        std::cerr << "WARNING: this permanently overwrites the current database and attachments directory\n"
                     "with the contents of " << backupDirectory.string() << ". This cannot be undone.\n"
                     "Back up your current data first if you have not already done so.\n"
                     "Re-run with --yes to proceed.\n";
        return 2;
    }
    if (!fs::exists(backupDirectory)) {
        std::cerr << "Backup directory does not exist: " << backupDirectory.string() << "\n";
        return 2;
    }

    auto database = TicketHub::Infrastructure::Database::createDatabase(config);
    database->restore(backupDirectory.string());
    database->migrate();

    const fs::path attachmentsBackup = backupDirectory / "attachments";
    if (fs::exists(attachmentsBackup)) {
        fs::create_directories(config.attachmentsRoot);
        for (const auto& entry : fs::directory_iterator(attachmentsBackup)) {
            fs::copy(entry.path(), fs::path(config.attachmentsRoot) / entry.path().filename(),
                     fs::copy_options::recursive | fs::copy_options::copy_symlinks
                         | fs::copy_options::overwrite_existing);
        }
    }

    std::cout << "Restore complete: " << backupDirectory.string() << " (" << database->backendName()
              << "), pending migrations applied.\n";
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

        if (command == "backup") {
            return runBackup(config, argc, argv);
        }

        if (command == "restore") {
            return runRestore(config, argc, argv);
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
