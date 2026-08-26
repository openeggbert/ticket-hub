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
    // Demo seeding is OFF by default (security audit 2026-08-26, finding C1).
    // `migrations/*/002_seed_demo.sql` creates `demo@ticket-hub.local` as a
    // *global administrator* whose password ("demo12345") is written in that
    // file's own comment and whose Argon2id hash is committed to this public
    // repository -- so a default of `true` meant every start path except
    // docker-compose.yml handed an internet-facing installation a
    // known-credential admin account. Opt in explicitly
    // (TICKETHUB_SEED_DEMO=true, or `ticket-hub-cli seed-demo`) and only on a
    // loopback bind; `requireSafeDemoSeeding()` below refuses the dangerous
    // combination outright rather than trusting the operator to notice.
    bool seedDemo{false};
    // Deliberate, documented escape hatch for the one legitimate case the
    // loopback check cannot see: a container that binds 0.0.0.0 inside its
    // own network namespace while the port is published only to the host's
    // loopback. Naming it this way is the point -- an operator has to type
    // something that says "unsafe" to get past the guard.
    bool allowUnsafeDemoSeed{false};
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
    // Installation-wide ceiling on stored attachment bytes (security audit
    // 2026-08-26, finding M4). Per-file (25MB) and per-ticket (20) limits
    // already existed, but nothing capped the number of tickets, so any
    // project Member could fill the volume and take the database down with
    // it. Deployment-time configuration like every other storage setting,
    // not an admin-editable runtime option (D125 keeps limits as fixed
    // constants). 0 disables the check.
    std::int64_t attachmentsMaxTotalBytes{10LL * 1024 * 1024 * 1024}; // 10 GiB

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

    // True when `bindAddress` only accepts connections from this host.
    bool bindsToLoopbackOnly() const;
    // Throws std::runtime_error when demo seeding is enabled on a
    // non-loopback bind address (security audit 2026-08-26, finding C1).
    // Called from both entry points before any seeding happens.
    void requireSafeDemoSeeding() const;
};

} // namespace TicketHub::Config
