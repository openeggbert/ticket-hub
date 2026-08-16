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
    // webRoot/migrationsRoot/attachmentsRoot default relative to the process's
    // current working directory (./web, ./migrations, ./data/attachments), not
    // a compiled-in path -- so both the dev build (run from the repo root) and
    // an installed tree (run from the `cmake --install` prefix) pick up the
    // right files just by cd-ing there first. Override with
    // TICKETHUB_WEB_ROOT/TICKETHUB_MIGRATIONS_ROOT/TICKETHUB_ATTACHMENTS_DIR
    // when running from elsewhere (e.g. a real deployment's persistent,
    // backed-up attachments volume).
    std::string webRoot;
    std::string migrationsRoot;
    // Local filesystem attachment storage (D15, hardwired -- no S3/pluggable
    // backend).
    std::string attachmentsRoot;

    // Outbound email (D52, deferred-after-V1): SMTP only, configured like
    // the database connection string -- environment variables at deploy
    // time, never an admin-editable runtime setting (the password would
    // otherwise need to live in the database). Email delivery is enabled
    // (TicketService::emailDeliveryEnabled_) exactly when smtpHost is
    // non-empty; every other smtp* field is meaningless until then. See
    // src/infrastructure/delivery/SmtpEmailSender.h for how these are used
    // (only linked into ticket-hub-cli's `process-outbox` command -- the
    // server itself never makes an outbound network call).
    std::string smtpHost;
    std::uint16_t smtpPort{587};
    std::string smtpUsername;
    std::string smtpPassword;
    std::string smtpFromAddress;
    bool smtpUseTls{true};

    static AppConfig fromEnvironment();
};

} // namespace TicketHub::Config
