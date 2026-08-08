#include "application/AuthService.h"
#include "common/FileUtil.h"
#include "common/Sha256.h"
#include "config/Config.h"
#include "infrastructure/database/DatabaseFactory.h"
#include "infrastructure/delivery/SmtpEmailSender.h"
#include "infrastructure/delivery/WebhookDeliveryClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
        << "  restore <backup-directory> --yes --maintenance Restore the database and attachments directory\n"
        << "                                                from <backup-directory>, overwriting the current\n"
        << "                                                ones. Destructive and irreversible; requires\n"
        << "                                                --yes and --maintenance. Validates the backup\n"
        << "                                                manifest, then runs pending migrations. Stop the\n"
        << "                                                server first (D108).\n"
        << "  verify-backup <backup-directory>              Verify the backup manifest, database dump and\n"
        << "                                                every attachment checksum without restoring.\n"
        << "  process-outbox                               Attempt delivery of every pending webhook\n"
        << "                                                (D39/D41) and email (D52) row whose retry\n"
        << "                                                time has arrived. Not run automatically --\n"
        << "                                                intended to be cron-scheduled (every 1-5\n"
        << "                                                minutes is reasonable). The server itself\n"
        << "                                                never makes an outbound network call; this\n"
        << "                                                command is the only place that does.\n"
        << "  version                                      Print the Ticket Hub version.\n";
}

constexpr const char* BackupManifestFileName = "ticket-hub-backup.manifest";

struct BackupFileEntry {
    std::string relativePath;
    std::uintmax_t byteSize{};
    std::string sha256;
};

std::string databaseMigrationDirectory(const TicketHub::Config::AppConfig& config) {
    return config.databaseDriver == "sqlite" ? config.migrationsRoot + "/sqlite"
                                              : config.migrationsRoot + "/postgresql";
}

std::string migrationCatalogChecksum(const TicketHub::Config::AppConfig& config) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    const fs::path directory(databaseMigrationDirectory(config));
    if (fs::exists(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".sql") {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());
    std::string catalog;
    for (const auto& file : files) {
        catalog += file.filename().string() + ":" + TicketHub::Common::sha256Hex(TicketHub::Common::readTextFile(file.string())) + "\n";
    }
    return TicketHub::Common::sha256Hex(catalog);
}

std::vector<BackupFileEntry> backupFiles(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    std::vector<BackupFileEntry> entries;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().filename() == BackupManifestFileName) {
            continue;
        }
        const auto relative = fs::relative(entry.path(), directory).generic_string();
        entries.push_back({relative, entry.file_size(), TicketHub::Common::sha256Hex(TicketHub::Common::readTextFile(entry.path().string()))});
    }
    std::sort(entries.begin(), entries.end(), [](const BackupFileEntry& left, const BackupFileEntry& right) {
        return left.relativePath < right.relativePath;
    });
    return entries;
}

void writeBackupManifest(const std::filesystem::path& directory,
                         const TicketHub::Config::AppConfig& config,
                         const std::string& backendName) {
    std::ofstream manifest(directory / BackupManifestFileName, std::ios::binary | std::ios::trunc);
    if (!manifest) {
        throw std::runtime_error("Cannot write backup manifest");
    }
    manifest << "format=1\n"
             << "ticket_hub_version=" << TICKETHUB_VERSION << "\n"
             << "database_backend=" << backendName << "\n"
             << "migration_catalog_sha256=" << migrationCatalogChecksum(config) << "\n";
    for (const auto& file : backupFiles(directory)) {
        manifest << "file=" << file.relativePath << '\t' << file.byteSize << '\t' << file.sha256 << '\n';
    }
    if (!manifest) {
        throw std::runtime_error("Cannot finish backup manifest");
    }
}

bool safeRelativeBackupPath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

void verifyBackupManifest(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    std::ifstream manifest(directory / BackupManifestFileName, std::ios::binary);
    if (!manifest) {
        throw std::runtime_error("Backup manifest not found: " + (directory / BackupManifestFileName).string());
    }
    std::string line;
    bool formatSeen = false;
    int fileCount = 0;
    while (std::getline(manifest, line)) {
        if (line == "format=1") {
            formatSeen = true;
            continue;
        }
        if (line.rfind("file=", 0) != 0) {
            continue;
        }
        const std::string entry = line.substr(5);
        const auto firstTab = entry.find('\t');
        const auto secondTab = firstTab == std::string::npos ? std::string::npos : entry.find('\t', firstTab + 1);
        if (firstTab == std::string::npos || secondTab == std::string::npos) {
            throw std::runtime_error("Malformed backup manifest file entry");
        }
        const fs::path relative(entry.substr(0, firstTab));
        if (!safeRelativeBackupPath(relative)) {
            throw std::runtime_error("Unsafe path in backup manifest: " + relative.string());
        }
        const std::uintmax_t expectedSize = std::stoull(entry.substr(firstTab + 1, secondTab - firstTab - 1));
        const std::string expectedDigest = entry.substr(secondTab + 1);
        const fs::path file = directory / relative;
        if (!fs::is_regular_file(file)) {
            throw std::runtime_error("Backup file missing: " + relative.string());
        }
        if (fs::file_size(file) != expectedSize) {
            throw std::runtime_error("Backup file size mismatch: " + relative.string());
        }
        if (TicketHub::Common::sha256Hex(TicketHub::Common::readTextFile(file.string())) != expectedDigest) {
            throw std::runtime_error("Backup file checksum mismatch: " + relative.string());
        }
        ++fileCount;
    }
    if (!formatSeen || fileCount == 0) {
        throw std::runtime_error("Backup manifest is incomplete or unsupported");
    }
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
    writeBackupManifest(outputDirectory, config, database->backendName());

    std::cout << "Backup complete and verified manifest written: " << outputDirectory.string() << " ("
              << database->backendName() << ")\n";
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
    bool maintenanceConfirmed = false;
    for (int index = 3; index < argc; ++index) {
        if (std::string(argv[index]) == "--yes") {
            confirmed = true;
        }
        if (std::string(argv[index]) == "--maintenance") {
            maintenanceConfirmed = true;
        }
    }
    if (!confirmed || !maintenanceConfirmed) {
        std::cerr << "WARNING: this permanently overwrites the current database and attachments directory\n"
                     "with the contents of " << backupDirectory.string() << ". This cannot be undone.\n"
                     "Stop Ticket Hub first, back up current data, and re-run with --yes --maintenance\n"
                     "to acknowledge the required maintenance window.\n";
        return 2;
    }
    if (!fs::exists(backupDirectory)) {
        std::cerr << "Backup directory does not exist: " << backupDirectory.string() << "\n";
        return 2;
    }
    verifyBackupManifest(backupDirectory);

    auto database = TicketHub::Infrastructure::Database::createDatabase(config);
    database->restore(backupDirectory.string());
    database->migrate();

    const fs::path attachmentsTarget(config.attachmentsRoot);
    if (attachmentsTarget.empty() || attachmentsTarget == attachmentsTarget.root_path()) {
        throw std::runtime_error("Refusing to replace an unsafe attachments directory");
    }
    // A backup with no attachments must restore to no attachments too. Clear
    // the target only after the manifest/database preflight and explicit
    // maintenance confirmation above, avoiding stale files from the newer
    // installation surviving an otherwise successful restore.
    fs::remove_all(attachmentsTarget);
    const fs::path attachmentsBackup = backupDirectory / "attachments";
    if (fs::exists(attachmentsBackup)) {
        fs::create_directories(attachmentsTarget);
        for (const auto& entry : fs::directory_iterator(attachmentsBackup)) {
            fs::copy(entry.path(), attachmentsTarget / entry.path().filename(),
                     fs::copy_options::recursive | fs::copy_options::copy_symlinks
                         | fs::copy_options::overwrite_existing);
        }
    }

    std::cout << "Restore complete: " << backupDirectory.string() << " (" << database->backendName()
              << "), pending migrations applied.\n";
    return 0;
}

int runVerifyBackup(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: verify-backup <backup-directory>\n";
        return 2;
    }
    const std::filesystem::path backupDirectory(argv[2]);
    if (!std::filesystem::is_directory(backupDirectory)) {
        std::cerr << "Backup directory does not exist: " << backupDirectory.string() << "\n";
        return 2;
    }
    verifyBackupManifest(backupDirectory);
    std::cout << "Backup manifest verified: " << backupDirectory.string() << '\n';
    return 0;
}

// Durable outbox delivery (D39/D41 webhooks, D52 email; see
// migrations/*/019_outbox_delivery.sql). This is the ONLY place in the
// entire application that makes an outbound network call -- the server
// (src/main.cpp) only ever writes durable delivery rows, never sends them,
// so a slow or unreachable external endpoint can never block a
// request-handling thread. Intended to be cron-scheduled by the admin, the
// same "explicit CLI step, not an automatic background job" posture
// backup/restore/migrate already use. Processes a fixed-size batch per
// call (rather than looping until empty) so a single invocation has a
// predictable running time even with a large backlog -- a cron running
// every few minutes will simply catch up over subsequent runs.
int runProcessOutbox(const TicketHub::Config::AppConfig& config) {
    using TicketHub::Infrastructure::Delivery::sendWebhookDelivery;
    using TicketHub::Infrastructure::Delivery::sendEmail;
    using TicketHub::Infrastructure::Delivery::SmtpConfig;

    constexpr int BatchSize = 100;
    auto database = TicketHub::Infrastructure::Database::createDatabase(config);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    int webhooksDelivered = 0;
    int webhooksFailed = 0;
    for (const auto& delivery : database->listPendingWebhookDeliveries(BatchSize)) {
        const auto result = sendWebhookDelivery(delivery.targetUrl, delivery.secret, delivery.payload);
        database->recordWebhookDeliveryResult(delivery.id, result.success,
            result.success ? std::nullopt : std::optional<std::string>(result.error));
        if (result.success) {
            ++webhooksDelivered;
        } else {
            ++webhooksFailed;
            std::cerr << "webhook delivery " << delivery.id << " (" << delivery.eventType << ") failed: "
                      << result.error << " (attempt " << (delivery.attemptCount + 1) << "/"
                      << TicketHub::Domain::MaxDeliveryAttempts << ")\n";
        }
    }

    int emailsSent = 0;
    int emailsFailed = 0;
    if (config.smtpHost.empty()) {
        std::cout << "SMTP not configured (TICKETHUB_SMTP_HOST unset) -- skipping email delivery.\n";
    } else {
        SmtpConfig smtp;
        smtp.host = config.smtpHost;
        smtp.port = config.smtpPort;
        smtp.username = config.smtpUsername;
        smtp.password = config.smtpPassword;
        smtp.fromAddress = config.smtpFromAddress;
        smtp.useTls = config.smtpUseTls;
        for (const auto& delivery : database->listPendingEmailDeliveries(BatchSize)) {
            const auto result = sendEmail(smtp, delivery.recipientEmail, delivery.subject, delivery.body);
            database->recordEmailDeliveryResult(delivery.id, result.success,
                result.success ? std::nullopt : std::optional<std::string>(result.error));
            if (result.success) {
                ++emailsSent;
            } else {
                ++emailsFailed;
                std::cerr << "email delivery " << delivery.id << " to " << delivery.recipientEmail
                          << " failed: " << result.error << " (attempt " << (delivery.attemptCount + 1) << "/"
                          << TicketHub::Domain::MaxDeliveryAttempts << ")\n";
            }
        }
    }

    curl_global_cleanup();

    std::cout << "Webhooks: " << webhooksDelivered << " delivered, " << webhooksFailed << " failed this run.\n"
              << "Email: " << emailsSent << " sent, " << emailsFailed << " failed this run.\n";
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

        if (command == "verify-backup") {
            return runVerifyBackup(argc, argv);
        }

        if (command == "process-outbox") {
            if (argc != 2) {
                printUsage(argv[0]);
                return 2;
            }
            return runProcessOutbox(config);
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
