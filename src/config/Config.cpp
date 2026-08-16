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
    config.webRoot = envOr("TICKETHUB_WEB_ROOT", "./web");
    config.migrationsRoot = envOr("TICKETHUB_MIGRATIONS_ROOT", "./migrations");
    config.attachmentsRoot = envOr("TICKETHUB_ATTACHMENTS_DIR", "./data/attachments");

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

} // namespace TicketHub::Config
