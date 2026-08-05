#include "web/JsonLogHandler.h"

#include <ctime>
#include <iostream>

namespace TicketHub::Web {
namespace {

const char* levelName(crow::LogLevel level) {
    switch (level) {
        case crow::LogLevel::Debug: return "debug";
        case crow::LogLevel::Info: return "info";
        case crow::LogLevel::Warning: return "warning";
        case crow::LogLevel::Error: return "error";
        case crow::LogLevel::Critical: return "critical";
    }
    return "info";
}

// ISO 8601 UTC, matching the timestamp format already used for the
// audit_events table (D23) and every migration's created_at column.
std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(buffer);
}

} // namespace

void JsonLogHandler::log(const std::string& message, crow::LogLevel level) {
    crow::json::wvalue entry;
    entry["timestamp"] = currentTimestamp();
    entry["level"] = levelName(level);
    entry["service"] = "ticket-hub";
    entry["message"] = message;
    std::cout << entry.dump() << std::endl;
}

} // namespace TicketHub::Web
