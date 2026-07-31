#include "web/HttpServer.h"

#include "common/FileUtil.h"
#include "web/Api.h"

#include <crow.h>
#include <exception>
#include <iostream>
#include <string>

namespace TicketHub::Web {
namespace {

crow::response staticResponse(const std::string& path, const std::string& contentType) {
    try {
        crow::response response(200, Common::readTextFile(path));
        response.set_header("Content-Type", contentType);
        response.set_header("Cache-Control", "no-cache");
        return response;
    } catch (const std::exception& error) {
        return crow::response(404, error.what());
    }
}

} // namespace

void runHttpServer(const Config::AppConfig& config,
                   const std::shared_ptr<Application::TicketService>& service,
                   const std::shared_ptr<Application::AuthService>& authService) {
    crow::SimpleApp app;
    registerApiRoutes(app, service, authService);

    CROW_ROUTE(app, "/")([root = config.webRoot] {
        return staticResponse(root + "/index.html", "text/html; charset=utf-8");
    });
    CROW_ROUTE(app, "/app.js")([root = config.webRoot] {
        return staticResponse(root + "/app.js", "text/javascript; charset=utf-8");
    });
    CROW_ROUTE(app, "/styles.css")([root = config.webRoot] {
        return staticResponse(root + "/styles.css", "text/css; charset=utf-8");
    });
    CROW_ROUTE(app, "/favicon.svg")([root = config.webRoot] {
        return staticResponse(root + "/favicon.svg", "image/svg+xml");
    });

    std::cout << "Ticket Hub " << TICKETHUB_VERSION << " listening on http://"
              << config.bindAddress << ':' << config.port
              << " using " << service->backendName() << '\n';

    app.bindaddr(config.bindAddress)
        .port(config.port)
        .multithreaded()
        .run();
}

} // namespace TicketHub::Web
