#include "web/HttpServer.h"

#include "common/FileUtil.h"
#include "web/Api.h"
#include "web/JsonLogHandler.h"

#include <crow.h>
#include <exception>
#include <iostream>
#include <string>

namespace TicketHub::Web {
namespace {

// Security hardening pass (Phase 6): baseline headers for every static
// response. `nosniff` stops the browser from MIME-sniffing app.js/styles.css
// as something else; `X-Frame-Options`/`frame-ancestors` and `Referrer-
// Policy` are cheap defense-in-depth for an app that is otherwise entirely
// same-origin, with no legitimate reason to be framed by another site.
void applyStaticSecurityHeaders(crow::response& response) {
    response.set_header("X-Content-Type-Options", "nosniff");
    response.set_header("X-Frame-Options", "DENY");
    response.set_header("Referrer-Policy", "same-origin");
}

crow::response staticResponse(const std::string& path, const std::string& contentType) {
    try {
        crow::response response(200, Common::readTextFile(path));
        response.set_header("Content-Type", contentType);
        response.set_header("Cache-Control", "no-cache");
        applyStaticSecurityHeaders(response);
        return response;
    } catch (const std::exception& error) {
        return crow::response(404, error.what());
    }
}

// A Content-Security-Policy is only meaningful on the HTML document itself
// (the thing a browser actually treats as a page with a script/style/frame
// context), not on app.js/styles.css/favicon.svg as raw asset responses, so
// this is applied only to "/" below rather than folded into
// `applyStaticSecurityHeaders`. `script-src 'self'` is the load-bearing
// clause -- it blocks any injected/inline `<script>` from executing at all,
// a second layer of defense behind the app's own Markdown-HTML sanitization
// and the sandboxed attachment-preview iframes. `style-src` allows
// `'unsafe-inline'` because `web/app.js` renders many inline `style="..."`
// attributes (layout tweaks, not user-controlled content) and reworking
// that to CSS classes is out of scope for this hardening pass -- inline
// styles cannot execute script, so this is a low-risk, deliberate
// exception, not an oversight.
void applyHtmlSecurityHeaders(crow::response& response) {
    applyStaticSecurityHeaders(response);
    response.set_header("Content-Security-Policy",
                        "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
                        "img-src 'self' data:; object-src 'none'; base-uri 'self'; "
                        "frame-ancestors 'none'; form-action 'self'");
}

} // namespace

void runHttpServer(const Config::AppConfig& config,
                   const std::shared_ptr<Application::TicketService>& service,
                   const std::shared_ptr<Application::AuthService>& authService) {
    // Must outlive app.run() below -- crow::logger keeps a raw, non-owning
    // pointer to whatever handler is set (Crow's own default handler is a
    // function-local static for the same reason).
    static JsonLogHandler jsonLogHandler;
    crow::logger::setHandler(&jsonLogHandler);

    crow::SimpleApp app;
    registerApiRoutes(app, service, authService);

    CROW_ROUTE(app, "/")([root = config.webRoot] {
        auto response = staticResponse(root + "/index.html", "text/html; charset=utf-8");
        applyHtmlSecurityHeaders(response);
        return response;
    });
    // Jira-style direct ticket links (e.g. /browse/TH-123): serves the exact
    // same single-page app shell as "/" -- `web/app.js` reads the ticket key
    // out of the URL on load (and keeps the URL in sync via
    // history.pushState as the user navigates) so a bookmarked/shared link
    // opens straight to that ticket. The `<string>` segment is never used
    // server-side; the actual key lookup/authorization happens through the
    // existing GET /api/v1/tickets/{key} route, same as any other ticket open.
    CROW_ROUTE(app, "/browse/<string>")([root = config.webRoot](const std::string&) {
        auto response = staticResponse(root + "/index.html", "text/html; charset=utf-8");
        applyHtmlSecurityHeaders(response);
        return response;
    });
    CROW_ROUTE(app, "/app.js")([root = config.webRoot] {
        return staticResponse(root + "/app.js", "text/javascript; charset=utf-8");
    });
    // Native ES modules keep the vanilla frontend decomposable without a
    // bundler. Assets remain explicit rather than exposing a generic
    // filesystem route, preserving the static-path traversal boundary.
    CROW_ROUTE(app, "/story-points.js")([root = config.webRoot] {
        return staticResponse(root + "/story-points.js", "text/javascript; charset=utf-8");
    });
    CROW_ROUTE(app, "/ticket-drawer-controls.js")([root = config.webRoot] {
        return staticResponse(root + "/ticket-drawer-controls.js", "text/javascript; charset=utf-8");
    });
    CROW_ROUTE(app, "/styles.css")([root = config.webRoot] {
        return staticResponse(root + "/styles.css", "text/css; charset=utf-8");
    });
    CROW_ROUTE(app, "/favicon.svg")([root = config.webRoot] {
        return staticResponse(root + "/favicon.svg", "image/svg+xml");
    });

    CROW_LOG_INFO << "Ticket Hub " << TICKETHUB_VERSION << " listening on http://"
                  << config.bindAddress << ':' << config.port
                  << " using " << service->backendName();

    app.bindaddr(config.bindAddress)
        .port(config.port)
        .multithreaded()
        .run();
}

} // namespace TicketHub::Web
