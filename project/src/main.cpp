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
        auto database = TicketHub::Infrastructure::Database::createDatabase(config);
        if (config.autoMigrate) {
            database->migrate();
        }
        if (config.seedDemo) {
            database->seedDemoData();
        }
        database->deleteExpiredSessions();
        auto service = std::make_shared<TicketHub::Application::TicketService>(database);
        auto authService = std::make_shared<TicketHub::Application::AuthService>(database);
        TicketHub::Web::runHttpServer(config, service, authService);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ticket Hub failed: " << error.what() << '\n';
        return 1;
    }
}
