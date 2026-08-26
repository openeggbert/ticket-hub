#include "application/AuthService.h"
#include "application/TicketService.h"
#include "config/Config.h"
#include "infrastructure/database/DatabaseFactory.h"
#include "web/HttpServer.h"

#include <exception>
#include <iostream>
#include <memory>

int main() {
    try {
        const auto config = TicketHub::Config::AppConfig::fromEnvironment();
        // Security audit 2026-08-26 (C1): refuse to start rather than seed a
        // known-credential global administrator onto a publicly reachable
        // bind address. Checked before the database is even opened.
        config.requireSafeDemoSeeding();
        auto database = TicketHub::Infrastructure::Database::createDatabase(config);
        if (config.autoMigrate) {
            database->migrate();
        }
        if (config.seedDemo) {
            database->seedDemoData();
        }
        database->deleteExpiredSessions();
        auto service = std::make_shared<TicketHub::Application::TicketService>(
            database, config.attachmentsRoot, !config.smtpHost.empty(), config.attachmentsMaxTotalBytes);
        auto authService = std::make_shared<TicketHub::Application::AuthService>(database);
        TicketHub::Web::runHttpServer(config, service, authService);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ticket Hub failed: " << error.what() << '\n';
        return 1;
    }
}
