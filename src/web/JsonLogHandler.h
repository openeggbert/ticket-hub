#pragma once

#include <crow.h>

namespace TicketHub::Web {

// Structured JSON logs to stdout (Phase 7, D133): "no Prometheus, no
// OpenTelemetry -- stdout logs sufficient for small self-hosted deployment
// (captured by Docker/systemd)." Replaces Crow's default `CerrLogHandler`
// (plain-text lines to stderr) so every log call Crow already makes
// internally (server startup, per-request Info-level lines, warnings/
// errors) becomes one JSON object per line on stdout instead -- no new
// call sites needed anywhere else in the app.
class JsonLogHandler : public crow::ILogHandler {
public:
    void log(const std::string& message, crow::LogLevel level) override;
};

} // namespace TicketHub::Web
