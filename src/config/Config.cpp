#include "config/Config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace TicketHub::Config {
namespace {

std::string envOr(const char* name, std::string fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::move(fallback) : std::string(value);
}

bool envBool(const char* name, bool fallback) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') {
        return fallback;
    }
    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    throw std::invalid_argument(std::string(name) + " must be a boolean");
}

} // namespace

AppConfig AppConfig::fromEnvironment() {
    AppConfig config;
    config.databaseDriver = envOr("TICKETHUB_DB_DRIVER", config.databaseDriver);
    std::transform(config.databaseDriver.begin(), config.databaseDriver.end(), config.databaseDriver.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    config.databaseUrl = envOr("TICKETHUB_DATABASE_URL", config.databaseUrl);
    config.sqlitePath = envOr("TICKETHUB_SQLITE_PATH", config.sqlitePath);
    config.bindAddress = envOr("TICKETHUB_BIND_ADDRESS", config.bindAddress);
    config.autoMigrate = envBool("TICKETHUB_AUTO_MIGRATE", config.autoMigrate);
    config.seedDemo = envBool("TICKETHUB_SEED_DEMO", config.seedDemo);
    config.allowUnsafeDemoSeed = envBool("TICKETHUB_ALLOW_UNSAFE_DEMO_SEED", config.allowUnsafeDemoSeed);
    config.webRoot = envOr("TICKETHUB_WEB_ROOT", "./web");
    config.migrationsRoot = envOr("TICKETHUB_MIGRATIONS_ROOT", "./migrations");
    config.attachmentsRoot = envOr("TICKETHUB_ATTACHMENTS_DIR", "./data/attachments");
    const auto maxTotalText =
        envOr("TICKETHUB_ATTACHMENTS_MAX_TOTAL_BYTES", std::to_string(config.attachmentsMaxTotalBytes));
    config.attachmentsMaxTotalBytes = std::stoll(maxTotalText);
    if (config.attachmentsMaxTotalBytes < 0) {
        throw std::invalid_argument("TICKETHUB_ATTACHMENTS_MAX_TOTAL_BYTES must not be negative");
    }

    const auto portText = envOr("TICKETHUB_PORT", std::to_string(config.port));
    const int parsedPort = std::stoi(portText);
    if (parsedPort < 1 || parsedPort > 65535) {
        throw std::invalid_argument("TICKETHUB_PORT must be between 1 and 65535");
    }
    config.port = static_cast<std::uint16_t>(parsedPort);

    config.smtpHost = envOr("TICKETHUB_SMTP_HOST", config.smtpHost);
    config.smtpUsername = envOr("TICKETHUB_SMTP_USERNAME", config.smtpUsername);
    config.smtpPassword = envOr("TICKETHUB_SMTP_PASSWORD", config.smtpPassword);
    config.smtpFromAddress = envOr("TICKETHUB_SMTP_FROM", config.smtpFromAddress);
    config.smtpUseTls = envBool("TICKETHUB_SMTP_USE_TLS", config.smtpUseTls);
    const auto smtpPortText = envOr("TICKETHUB_SMTP_PORT", std::to_string(config.smtpPort));
    const int parsedSmtpPort = std::stoi(smtpPortText);
    if (parsedSmtpPort < 1 || parsedSmtpPort > 65535) {
        throw std::invalid_argument("TICKETHUB_SMTP_PORT must be between 1 and 65535");
    }
    config.smtpPort = static_cast<std::uint16_t>(parsedSmtpPort);

    return config;
}

bool AppConfig::bindsToLoopbackOnly() const {
    // Deliberately a small, explicit set rather than a general parser: these
    // are the only spellings `TICKETHUB_BIND_ADDRESS` realistically takes for
    // a loopback-only bind, and anything unrecognized is treated as
    // publicly reachable (fail closed).
    return bindAddress == "127.0.0.1" || bindAddress == "localhost" || bindAddress == "::1" ||
           bindAddress.rfind("127.", 0) == 0;
}

void AppConfig::requireSafeDemoSeeding() const {
    if (!seedDemo || bindsToLoopbackOnly() || allowUnsafeDemoSeed) {
        return;
    }
    throw std::runtime_error(
        "Refusing to start: TICKETHUB_SEED_DEMO is enabled while TICKETHUB_BIND_ADDRESS is \""
        + bindAddress
        + "\". The demo seed creates a global administrator (demo@ticket-hub.local) whose password is "
          "published in migrations/*/002_seed_demo.sql, so it must never run on a publicly reachable "
          "bind address. Set TICKETHUB_SEED_DEMO=false, or bind to 127.0.0.1 for local development. If "
          "this process is in a container whose port is published only to the host's loopback, set "
          "TICKETHUB_ALLOW_UNSAFE_DEMO_SEED=true to acknowledge the risk explicitly.");
}

} // namespace TicketHub::Config
