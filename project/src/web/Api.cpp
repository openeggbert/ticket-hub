#include "web/Api.h"

#include "common/RandomToken.h"
#include "domain/Errors.h"
#include "web/RateLimiter.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TicketHub::Web {
namespace {

// Security hardening pass (Phase 6): baseline response headers applied to
// every API response. `X-Content-Type-Options: nosniff` stops a browser
// from MIME-sniffing a response (e.g. a JSON error body) as HTML/script;
// `X-Frame-Options`/`Referrer-Policy` are cheap defense-in-depth for an app
// that is otherwise entirely same-origin. No CSP here -- API responses
// aren't rendered as documents; the HTML page gets its own, stricter CSP
// in `HttpServer.cpp`.
void applySecurityHeaders(crow::response& response) {
    response.set_header("X-Content-Type-Options", "nosniff");
    response.set_header("X-Frame-Options", "DENY");
    response.set_header("Referrer-Policy", "same-origin");
}

crow::response jsonResponse(int status, crow::json::wvalue body) {
    crow::response response(status, body.dump());
    response.set_header("Content-Type", "application/json; charset=utf-8");
    response.set_header("Cache-Control", "no-store");
    applySecurityHeaders(response);
    return response;
}

crow::response errorResponse(int status, const std::string& message) {
    crow::json::wvalue body;
    body["error"] = message;
    return jsonResponse(status, std::move(body));
}

// D124's original description pairs a 429 with a Retry-After header; the V1
// simplification only dropped the admin-configurable multi-level limits,
// not this response contract, so both fixed limiters attach it using their
// own fixed window length.
crow::response rateLimitedResponse(const std::string& message, int retryAfterSeconds) {
    auto response = errorResponse(429, message);
    response.set_header("Retry-After", std::to_string(retryAfterSeconds));
    return response;
}

crow::json::wvalue userJson(const Domain::UserSummary& user) {
    crow::json::wvalue json;
    json["id"] = user.id;
    json["displayName"] = user.displayName;
    json["email"] = user.email;
    return json;
}

crow::json::wvalue principalJson(const Domain::Principal& principal) {
    crow::json::wvalue json;
    json["userId"] = principal.userId;
    json["email"] = principal.email;
    json["displayName"] = principal.displayName;
    json["isAdmin"] = principal.isAdmin;
    json["timeZone"] = principal.timeZone;
    json["clockFormat"] = principal.clockFormat;
    return json;
}

// Never includes the raw token (D40) -- only tokenJson(CreatedPersonalAccessToken)
// does, and only in the create response.
crow::json::wvalue tokenJson(const Domain::PersonalAccessToken& token) {
    crow::json::wvalue json;
    json["id"] = token.id;
    json["name"] = token.name;
    json["createdAt"] = token.createdAt;
    json["expiresAt"] = token.expiresAt;
    json["lastUsedAt"] = token.lastUsedAt ? crow::json::wvalue(*token.lastUsedAt) : crow::json::wvalue(nullptr);
    json["revokedAt"] = token.revokedAt ? crow::json::wvalue(*token.revokedAt) : crow::json::wvalue(nullptr);
    return json;
}

crow::json::wvalue sessionJson(const Domain::Session& session, const bool isCurrent) {
    crow::json::wvalue json;
    json["id"] = session.id;
    json["createdAt"] = session.createdAt;
    json["expiresAt"] = session.expiresAt;
    json["isCurrent"] = isCurrent;
    return json;
}

crow::json::wvalue projectJson(const Domain::Project& project) {
    crow::json::wvalue json;
    json["id"] = project.id;
    json["key"] = project.key;
    json["name"] = project.name;
    json["description"] = project.description;
    json["ticketCount"] = project.ticketCount;
    json["openTicketCount"] = project.openTicketCount;
    if (project.lead) {
        json["lead"] = userJson(*project.lead);
    } else {
        json["lead"] = nullptr;
    }
    return json;
}

crow::json::wvalue componentSummaryJson(const Domain::ComponentSummary& component) {
    crow::json::wvalue json;
    json["id"] = component.id;
    json["name"] = component.name;
    return json;
}

crow::json::wvalue componentJson(const Domain::ProjectComponent& component) {
    crow::json::wvalue json;
    json["id"] = component.id;
    json["projectKey"] = component.projectKey;
    json["name"] = component.name;
    json["description"] = component.description;
    json["lead"] = component.lead ? userJson(*component.lead) : crow::json::wvalue(nullptr);
    json["defaultAssignee"] = component.defaultAssignee ? userJson(*component.defaultAssignee) : crow::json::wvalue(nullptr);
    json["createdAt"] = component.createdAt;
    json["updatedAt"] = component.updatedAt;
    return json;
}

crow::json::wvalue ticketJson(const Domain::Ticket& ticket) {
    crow::json::wvalue json;
    json["id"] = ticket.id;
    json["key"] = ticket.key;
    json["number"] = ticket.number;
    json["projectKey"] = ticket.projectKey;
    json["projectName"] = ticket.projectName;
    json["summary"] = ticket.summary;
    json["description"] = ticket.description;
    json["type"] = crow::json::wvalue{{"key", ticket.type.key},
                                      {"name", ticket.type.name},
                                      {"icon", ticket.type.icon},
                                      {"color", ticket.type.color}};
    json["status"] = crow::json::wvalue{{"key", ticket.status.key},
                                        {"name", ticket.status.name},
                                        {"category", ticket.status.category},
                                        {"sortOrder", ticket.status.sortOrder}};
    json["priority"] = crow::json::wvalue{{"key", ticket.priority.key},
                                          {"name", ticket.priority.name},
                                          {"rank", ticket.priority.rank},
                                          {"color", ticket.priority.color}};
    json["reporter"] = userJson(ticket.reporter);
    json["assignee"] = ticket.assignee ? userJson(*ticket.assignee) : crow::json::wvalue(nullptr);
    json["parentTicketKey"] = ticket.parentTicketKey ? crow::json::wvalue(*ticket.parentTicketKey) : crow::json::wvalue(nullptr);
    json["component"] = ticket.component ? componentSummaryJson(*ticket.component) : crow::json::wvalue(nullptr);
    json["storyPoints"] = ticket.storyPoints ? crow::json::wvalue(*ticket.storyPoints) : crow::json::wvalue(nullptr);
    json["dueDate"] = ticket.dueDate ? crow::json::wvalue(*ticket.dueDate) : crow::json::wvalue(nullptr);
    json["resolution"] = ticket.resolution ? crow::json::wvalue(*ticket.resolution) : crow::json::wvalue(nullptr);
    crow::json::wvalue::list labels;
    for (const auto& label : ticket.labels) {
        labels.emplace_back(label);
    }
    json["labels"] = std::move(labels);
    json["createdAt"] = ticket.createdAt;
    json["updatedAt"] = ticket.updatedAt;
    json["version"] = ticket.version;
    json["rankOrder"] = ticket.rankOrder;
    return json;
}

// RFC 4180-style CSV field escaping: any field containing a comma, quote, or
// newline is wrapped in quotes with internal quotes doubled. Applied
// unconditionally (even to fields that happen not to need it) since it is
// always correct and keeps the call sites simple.
//
// Also neutralizes CSV/formula injection: every field here comes from
// user-controlled ticket content (summary, description, labels, ...), and a
// spreadsheet application (Excel, Google Sheets, LibreOffice Calc) treats a
// cell beginning with `=`, `+`, `-`, or `@` as a formula to evaluate when the
// file is opened, regardless of the exporting application's intent. A
// leading apostrophe is the standard mitigation (OWASP CSV Injection): every
// major spreadsheet application treats it as "force this cell to plain
// text" and does not display the apostrophe itself.
std::string csvField(const std::string& value) {
    std::string field = value;
    if (!field.empty() && (field.front() == '=' || field.front() == '+' || field.front() == '-' || field.front() == '@')) {
        field.insert(field.begin(), '\'');
    }
    const bool needsQuoting = field.find_first_of(",\"\n\r") != std::string::npos;
    if (!needsQuoting) {
        return field;
    }
    std::string escaped = "\"";
    for (const char c : field) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += '"';
    return escaped;
}

std::string ticketsToCsv(const std::vector<Domain::Ticket>& tickets) {
    std::ostringstream csv;
    csv << "key,project,summary,description,type,status,priority,reporter,assignee,storyPoints,dueDate,"
           "resolution,labels,createdAt,updatedAt\r\n";
    for (const auto& ticket : tickets) {
        std::ostringstream labels;
        for (std::size_t i = 0; i < ticket.labels.size(); ++i) {
            if (i > 0) labels << ';';
            labels << ticket.labels[i];
        }
        csv << csvField(ticket.key) << ',' << csvField(ticket.projectKey) << ',' << csvField(ticket.summary)
            << ',' << csvField(ticket.description) << ',' << csvField(ticket.type.name) << ','
            << csvField(ticket.status.name) << ',' << csvField(ticket.priority.name) << ','
            << csvField(ticket.reporter.email) << ','
            << csvField(ticket.assignee ? ticket.assignee->email : "") << ',';
        if (ticket.storyPoints) csv << *ticket.storyPoints;
        csv << ',' << csvField(ticket.dueDate.value_or("")) << ',' << csvField(ticket.resolution.value_or(""))
            << ',' << csvField(labels.str()) << ',' << csvField(ticket.createdAt) << ','
            << csvField(ticket.updatedAt) << "\r\n";
    }
    return csv.str();
}

crow::json::wvalue commentJson(const Domain::Comment& comment) {
    crow::json::wvalue json;
    json["id"] = comment.id;
    json["ticketId"] = comment.ticketId;
    json["author"] = userJson(comment.author);
    json["body"] = comment.body;
    json["createdAt"] = comment.createdAt;
    json["updatedAt"] = comment.updatedAt;
    json["version"] = comment.version;
    json["editedAt"] = comment.editedAt ? crow::json::wvalue(*comment.editedAt) : crow::json::wvalue(nullptr);
    return json;
}

crow::json::wvalue auditEventJson(const Domain::AuditEvent& event) {
    crow::json::wvalue json;
    json["id"] = event.id;
    json["category"] = event.category;
    json["action"] = event.action;
    json["actor"] = event.actor ? userJson(*event.actor) : crow::json::wvalue(nullptr);
    json["targetType"] = event.targetType ? crow::json::wvalue(*event.targetType) : crow::json::wvalue(nullptr);
    json["targetId"] = event.targetId ? crow::json::wvalue(*event.targetId) : crow::json::wvalue(nullptr);
    json["details"] = event.details ? crow::json::wvalue(*event.details) : crow::json::wvalue(nullptr);
    json["createdAt"] = event.createdAt;
    return json;
}

crow::json::wvalue worklogJson(const Domain::Worklog& worklog) {
    crow::json::wvalue json;
    json["id"] = worklog.id;
    json["ticketId"] = worklog.ticketId;
    json["author"] = userJson(worklog.author);
    json["workDate"] = worklog.workDate;
    json["timeSpentSeconds"] = worklog.timeSpentSeconds;
    json["comment"] = worklog.comment ? crow::json::wvalue(*worklog.comment) : crow::json::wvalue(nullptr);
    json["createdAt"] = worklog.createdAt;
    json["updatedAt"] = worklog.updatedAt;
    json["version"] = worklog.version;
    return json;
}

crow::json::wvalue attachmentJson(const Domain::Attachment& attachment) {
    crow::json::wvalue json;
    json["id"] = attachment.id;
    json["ticketId"] = attachment.ticketId;
    json["ticketKey"] = attachment.ticketKey;
    json["uploader"] = userJson(attachment.uploader);
    json["fileName"] = attachment.fileName;
    json["contentType"] = attachment.contentType;
    json["byteSize"] = attachment.byteSize;
    json["sha256"] = attachment.sha256;
    json["createdAt"] = attachment.createdAt;
    return json;
}

// Defends against HTTP response-splitting via a CR/LF in a user-supplied
// file name embedded into a response header (Content-Disposition).
std::string sanitizeHeaderValue(const std::string& value) {
    std::string sanitized = value;
    sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(),
                                   [](const unsigned char ch) { return ch == '\r' || ch == '\n' || ch == '"'; }),
                    sanitized.end());
    return sanitized;
}

// Security hardening pass (Phase 6): D98 deliberately has no upload-time
// MIME allow-list, so `attachment.contentType` is caller-supplied and
// untrusted -- the multipart upload route stores whatever `Content-Type` the
// uploading client declared, verbatim. Serving that value back with
// `Content-Disposition: inline` would let an attacker upload a file (any
// extension not on D98's blocked-extension list, e.g. "notes.txt") with a
// spoofed `Content-Type: text/html` body containing `<script>`, then have it
// render as an HTML document -- either via direct download-URL navigation,
// or inside the app's own unsandboxed-at-the-time text/PDF `<iframe>`
// preview -- executing script same-origin (the sandboxed-iframe fix in
// `web/app.js` closes the iframe path; this closes the direct-navigation
// path). Only content types that cannot execute script when rendered
// directly by a browser get `inline`; everything else -- explicitly
// including HTML/XHTML/SVG/XML and script MIME types -- is forced to
// `attachment` (a forced download, never rendered as a document). `<img>`/
// `<audio>`/`<video>` tag rendering is unaffected either way, since those
// elements do not honor `Content-Disposition`.
bool contentTypeSafeToRenderInline(const std::string& contentType) {
    static const std::vector<std::string> unsafePrefixes = {
        "text/html", "application/xhtml", "image/svg", "application/xml", "text/xml",
        "application/xslt", "application/javascript", "text/javascript", "application/ecmascript",
    };
    for (const auto& unsafe : unsafePrefixes) {
        if (contentType.rfind(unsafe, 0) == 0) {
            return false;
        }
    }
    return true;
}

crow::json::wvalue commentReactionJson(const Domain::CommentReaction& reaction) {
    crow::json::wvalue json;
    json["reactionKey"] = reaction.reactionKey;
    json["user"] = userJson(reaction.user);
    return json;
}

// Deliberately narrower than the full Domain::User (no isAdmin/active/
// timeZone/clockFormat) -- this is a directory listing for @mention
// autocomplete and assignee pickers, not an admin user-management view.
crow::json::wvalue userDirectoryJson(const Domain::User& user) {
    crow::json::wvalue json;
    json["id"] = user.id;
    json["displayName"] = user.displayName;
    json["email"] = user.email;
    json["handle"] = user.handle ? crow::json::wvalue(*user.handle) : crow::json::wvalue(nullptr);
    return json;
}

crow::json::wvalue notificationJson(const Domain::Notification& notification) {
    crow::json::wvalue json;
    json["id"] = notification.id;
    json["type"] = notification.type;
    json["ticketKey"] = notification.ticketKey ? crow::json::wvalue(*notification.ticketKey) : crow::json::wvalue(nullptr);
    json["ticketSummary"] =
        notification.ticketSummary ? crow::json::wvalue(*notification.ticketSummary) : crow::json::wvalue(nullptr);
    json["readAt"] = notification.readAt ? crow::json::wvalue(*notification.readAt) : crow::json::wvalue(nullptr);
    json["createdAt"] = notification.createdAt;
    return json;
}

crow::json::wvalue ticketLinkJson(const Domain::TicketLink& link) {
    const auto labels = Domain::linkTypeLabels(link.linkType);
    crow::json::wvalue json;
    json["id"] = link.id;
    json["linkType"] = link.linkType;
    json["label"] = link.outward ? labels.outward : labels.inward;
    json["otherTicketKey"] = link.otherTicketKey;
    json["otherTicketSummary"] = link.otherTicketSummary;
    return json;
}

crow::json::wvalue bulkActionResultJson(const Domain::BulkActionResult& result) {
    crow::json::wvalue::list succeeded;
    for (const auto& key : result.succeeded) {
        succeeded.emplace_back(key);
    }
    crow::json::wvalue::list failed;
    for (const auto& key : result.failed) {
        failed.emplace_back(key);
    }
    crow::json::wvalue json;
    json["succeeded"] = std::move(succeeded);
    json["failed"] = std::move(failed);
    return json;
}

// Fixed batch-size constant (D125): "fixed constants only (max body size,
// max bulk items, max page size); no admin exceptions." Applies to every
// `POST /api/v1/tickets/bulk/*` route's `ticketKeys` array via the shared
// `requiredTicketKeys` helper below.
constexpr std::size_t MaxBulkItems = 200;

// Simple bulk actions (D36) always take {"ticketKeys": [...]} plus
// action-specific fields; every bulk route needs this.
std::vector<std::string> requiredTicketKeys(const crow::json::rvalue& body) {
    if (!body.has("ticketKeys") || body["ticketKeys"].t() != crow::json::type::List) {
        throw std::invalid_argument("ticketKeys must be an array of strings");
    }
    std::vector<std::string> keys;
    for (const auto& item : body["ticketKeys"]) {
        if (item.t() != crow::json::type::String) {
            throw std::invalid_argument("ticketKeys must be an array of strings");
        }
        keys.emplace_back(item.s());
    }
    if (keys.empty()) {
        throw std::invalid_argument("ticketKeys must not be empty");
    }
    if (keys.size() > MaxBulkItems) {
        throw std::invalid_argument("ticketKeys must not contain more than " + std::to_string(MaxBulkItems) + " items");
    }
    return keys;
}

std::optional<std::string> queryParameter(const crow::request& request, const char* name) {
    const char* value = request.url_params.get(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

// Numbered/offset pagination (D126): a missing `page`/`pageSize` query
// parameter returns `defaultValue` (used by TicketService::listTicketsPaged
// to clamp into range); a present-but-non-numeric one is a genuine client
// error, not silently ignored.
std::optional<int> optionalIntQueryParameter(const crow::request& request, const char* name) {
    const auto raw = queryParameter(request, name);
    if (!raw) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(*raw, &consumed);
        if (consumed != raw->size()) {
            throw std::invalid_argument(*raw);
        }
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(name) + " must be an integer");
    }
}

// Read-only CSV export of tickets (D48): no CSV import, no Jira migration
// tool. Shares the same `Domain::TicketFilter` query parameters and
// authorization as `GET /api/v1/tickets`, so an export can be scoped to
// whatever the caller could already see via the list view.
Domain::TicketFilter ticketFilterFromQuery(const crow::request& request) {
    Domain::TicketFilter filter;
    filter.projectKey = queryParameter(request, "project");
    filter.statusKey = queryParameter(request, "status");
    filter.ticketTypeKey = queryParameter(request, "type");
    filter.priorityKey = queryParameter(request, "priority");
    filter.assigneeEmail = queryParameter(request, "assignee");
    filter.label = queryParameter(request, "label");
    filter.componentName = queryParameter(request, "component");
    filter.dueBefore = queryParameter(request, "dueBefore");
    filter.search = queryParameter(request, "q");
    filter.sortByRank = queryParameter(request, "sort") == "rank";
    return filter;
}

std::string requiredString(const crow::json::rvalue& body, const char* field) {
    if (!body.has(field) || body[field].t() != crow::json::type::String) {
        throw std::invalid_argument(std::string(field) + " must be a string");
    }
    return body[field].s();
}

std::optional<std::string> optionalString(const crow::json::rvalue& body, const char* field) {
    if (!body.has(field) || body[field].t() == crow::json::type::Null) {
        return std::nullopt;
    }
    if (body[field].t() != crow::json::type::String) {
        throw std::invalid_argument(std::string(field) + " must be a string or null");
    }
    const std::string value = body[field].s();
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

// --- Session/CSRF cookies ---
//
// Double-submit-cookie CSRF pattern: the CSRF token is set as a readable
// (non-HttpOnly) cookie at login; state-changing requests must echo it back
// in the X-CSRF-Token header. A cross-origin attacker page can neither read
// the cookie nor set a custom header on a simple form submission, so a
// mismatch reliably indicates a forged request. The session token itself is
// always HttpOnly.
constexpr const char* SessionCookieName = "th_session";
constexpr const char* CsrfCookieName = "th_csrf";
constexpr long long SessionCookieMaxAgeSeconds = 30LL * 24 * 60 * 60;

// Fixed request-body-size constant (D125): "fixed constants only (max body
// size, max bulk items, max page size); no admin exceptions." Max page size
// has no meaning yet -- numbered/offset pagination (D126) does not exist in
// V1 yet, so it is intentionally not implemented here; max bulk items is
// enforced above, next to `requiredTicketKeys`.
//
// Generous for this API's largest legitimate JSON payload (a
// full-replacement ticket edit with a long Markdown description) while still
// bounding worst-case processing of a malicious/broken client. Enforced
// against `request.body.size()` (i.e. after Crow has already buffered the
// body), not against the `Content-Length` header before reading -- Crow's
// SimpleApp has no built-in hook to reject an oversized body pre-buffer, so
// this caps what the application processes rather than what the socket
// layer buffers; a real internet-facing deployment should also enforce a
// body-size limit at a reverse proxy in front of it. The separate multipart
// attachment upload route already enforces its own stricter, purpose-built
// 25MB/file limit (D98) and is unaffected by this constant.
constexpr std::size_t MaxJsonRequestBodyBytes = 1024 * 1024; // 1 MiB

std::optional<std::string> cookieValue(const crow::request& request, const std::string& name) {
    const std::string header = request.get_header_value("Cookie");
    std::size_t position = 0;
    while (position < header.size()) {
        std::size_t separator = header.find(';', position);
        const std::string pair = header.substr(position, separator == std::string::npos ? std::string::npos : separator - position);
        const std::size_t equals = pair.find('=');
        if (equals != std::string::npos) {
            std::string key = pair.substr(0, equals);
            const auto firstNonSpace = key.find_first_not_of(' ');
            if (firstNonSpace != std::string::npos) {
                key = key.substr(firstNonSpace);
            }
            if (key == name) {
                return pair.substr(equals + 1);
            }
        }
        if (separator == std::string::npos) {
            break;
        }
        position = separator + 1;
    }
    return std::nullopt;
}

void addSessionCookies(crow::response& response, const std::string& sessionToken, const std::string& csrfToken) {
    std::ostringstream sessionCookie;
    sessionCookie << SessionCookieName << '=' << sessionToken
                  << "; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=" << SessionCookieMaxAgeSeconds;
    response.add_header("Set-Cookie", sessionCookie.str());

    std::ostringstream csrfCookie;
    csrfCookie << CsrfCookieName << '=' << csrfToken << "; Path=/; Secure; SameSite=Strict; Max-Age="
               << SessionCookieMaxAgeSeconds;
    response.add_header("Set-Cookie", csrfCookie.str());
}

void clearSessionCookies(crow::response& response) {
    response.add_header("Set-Cookie", std::string(SessionCookieName) + "=; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=0");
    response.add_header("Set-Cookie", std::string(CsrfCookieName) + "=; Path=/; Secure; SameSite=Strict; Max-Age=0");
}

// Bearer-token PAT authentication (Phase 6, D39/D40), tried only when no
// session cookie is present -- the browser-vs-API auth methods are
// deliberately mutually exclusive per request (D54).
std::optional<std::string> bearerToken(const crow::request& request) {
    const std::string header = request.get_header_value("Authorization");
    constexpr const char* prefix = "Bearer ";
    if (header.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    return header.substr(std::string(prefix).size());
}

// Resolves the caller's Principal from the session cookie, or (if absent) a
// PAT Bearer token. Returns nullopt (never throws) so route handlers can
// turn a missing/invalid session into a clean 401 response.
std::optional<Domain::Principal> resolvePrincipal(const crow::request& request,
                                                   const std::shared_ptr<Application::AuthService>& authService) {
    if (const auto token = cookieValue(request, SessionCookieName)) {
        return authService->validateSession(*token);
    }
    if (const auto token = bearerToken(request)) {
        return authService->validatePersonalAccessToken(*token);
    }
    return std::nullopt;
}

// CSRF only protects against a browser silently attaching a session cookie
// to a forged cross-origin request. A PAT Bearer token is never
// auto-attached by a browser, so a request with no session cookie in play
// is exempt -- whatever authenticated it (if anything), it wasn't a cookie.
bool csrfTokenValid(const crow::request& request) {
    if (!cookieValue(request, SessionCookieName)) {
        return true;
    }
    const auto cookie = cookieValue(request, CsrfCookieName);
    const std::string header = request.get_header_value("X-CSRF-Token");
    return cookie.has_value() && !cookie->empty() && *cookie == header;
}

// Fixed rate limits (Phase 6, D124/D125): "simple fixed rate limit per
// IP/user (e.g. login and write endpoints); no admin config, no
// per-endpoint/service-account exceptions." Two fixed, hardcoded limiters:
// one for the login endpoint (keyed by IP, since a not-yet-authenticated
// caller has no user id -- this is a distributed/enumeration defense that
// complements, not replaces, the existing per-account 10-attempts/
// 15-minute lockout in SqliteDatabase/PostgresDatabase), and one shared
// across all other write endpoints (keyed by user id when authenticated,
// else by IP). Process-lifetime in-memory state only, matching V1 having no
// shared cache/job infrastructure (docs/REMOVED_AND_DEFERRED_FEATURES.md).
RateLimiter& loginRateLimiter() {
    static RateLimiter limiter(20, std::chrono::minutes(15));
    return limiter;
}

RateLimiter& writeRateLimiter() {
    static RateLimiter limiter(120, std::chrono::minutes(1));
    return limiter;
}

bool loginRateLimitOk(const crow::request& request) {
    return loginRateLimiter().allow("ip:" + request.remote_ip_address);
}

// Shared by every write route (keyed by principal when authenticated) and by
// the handful of routes that resolve a differently-named principal-like
// variable instead of calling it `principal` (e.g. `current` for
// /api/v1/sessions/sign-out-others) via the userId overload below.
bool writeRateLimitOk(const crow::request& request, const std::optional<Domain::Principal>& principal) {
    const std::string key = principal ? "user:" + principal->userId : "ip:" + request.remote_ip_address;
    return writeRateLimiter().allow(key);
}

// Overload for the one write route (/api/v1/sessions/sign-out-others) that
// resolves a Domain::Session (`current`) instead of a Domain::Principal.
bool writeRateLimitOk(const crow::request&, const std::string& userId) {
    return writeRateLimiter().allow("user:" + userId);
}

} // namespace

void registerApiRoutes(crow::SimpleApp& app,
                       const std::shared_ptr<Application::TicketService>& service,
                       const std::shared_ptr<Application::AuthService>& authService) {
    CROW_ROUTE(app, "/api/health")([service] {
        crow::json::wvalue body;
        body["status"] = "ok";
        body["service"] = "ticket-hub";
        body["version"] = TICKETHUB_VERSION;
        body["database"] = service->backendName();
        return jsonResponse(200, std::move(body));
    });

    CROW_ROUTE(app, "/api/v1/auth/login")
    .methods(crow::HTTPMethod::Post)([authService](const crow::request& request) {
        if (!loginRateLimitOk(request)) {
            return rateLimitedResponse("Too many login attempts. Try again later.", 900);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::LoginRequest login;
            login.email = requiredString(body, "email");
            login.password = requiredString(body, "password");
            const auto authenticated = authService->login(std::move(login));

            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            auto response = jsonResponse(200, std::move(responseBody));
            // The CSRF half of the cookie pair does not need to be
            // cryptographically tied to the session -- it only needs to be
            // unguessable and readable solely by same-origin JS. It is
            // generated independently (not derived from the session token)
            // so that a leak of the non-HttpOnly CSRF cookie can never
            // expose any part of the HttpOnly session bearer secret.
            addSessionCookies(response, authenticated.sessionToken, Common::randomTokenHex(16));
            return response;
        } catch (const Domain::AccountLocked& error) {
            return errorResponse(423, error.what());
        } catch (const Domain::AuthenticationFailed& error) {
            return errorResponse(401, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/auth/logout")
    .methods(crow::HTTPMethod::Post)([authService](const crow::request& request) {
        // SameSite=Strict on th_session already blocks the session cookie
        // from being attached to any cross-site request, so a forged
        // logout is not presently reachable -- this check is defense in
        // depth, kept consistent with every other state-changing route.
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        const auto token = cookieValue(request, SessionCookieName);
        if (token) {
            authService->logout(*token);
        }
        crow::json::wvalue body;
        body["ok"] = true;
        auto response = jsonResponse(200, std::move(body));
        clearSessionCookies(response);
        return response;
    });

    CROW_ROUTE(app, "/api/v1/auth/me")([authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        return jsonResponse(200, principalJson(*principal));
    });

    // Self-service timezone/clock-format preferences (D45). Own-account-only
    // by construction: TicketService::updatePreferences always targets
    // `actor.userId`, never a caller-supplied id.
    CROW_ROUTE(app, "/api/v1/account/preferences")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::UpdatePreferencesRequest update;
            update.timeZone = requiredString(body, "timeZone");
            update.clockFormat = requiredString(body, "clockFormat");
            return jsonResponse(200, principalJson(service->updatePreferences(update, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Personal access tokens (Phase 6, D39/D40): self-service, no admin-
    // managed tokens -- every route here operates only on the caller's own
    // tokens. Managing tokens is a web-UI action (session-cookie-
    // authenticated, like every other write in this app), even though the
    // tokens themselves authenticate API calls.
    CROW_ROUTE(app, "/api/v1/tokens")
    .methods(crow::HTTPMethod::Get)([authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue::list items;
            for (const auto& token : authService->listPersonalAccessTokens(principal->userId)) {
                items.emplace_back(tokenJson(token));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tokens")
    .methods(crow::HTTPMethod::Post)([authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body || !body.has("expiresInDays") || body["expiresInDays"].t() != crow::json::type::Number) {
                return errorResponse(400, "expiresInDays must be a number");
            }
            const auto created = authService->createPersonalAccessToken(
                principal->userId, requiredString(body, "name"), body["expiresInDays"].i());
            crow::json::wvalue responseBody = tokenJson(created.token);
            responseBody["token"] = created.rawToken;
            return jsonResponse(201, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tokens/<string>")
    .methods(crow::HTTPMethod::Delete)([authService](const crow::request& request, const std::string& tokenId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!authService->revokePersonalAccessToken(tokenId, principal->userId)) {
                return errorResponse(404, "Token not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Active-session list and "sign out everywhere" (Phase 6, D54,
    // resequenced from Phase 1). Deliberately session-cookie-only, not
    // resolvePrincipal (which would also accept a PAT Bearer token) --
    // "your active web sessions" has no meaning for a PAT-authenticated
    // caller, since a PAT is not a session at all.
    CROW_ROUTE(app, "/api/v1/sessions")
    .methods(crow::HTTPMethod::Get)([authService](const crow::request& request) {
        const auto token = cookieValue(request, SessionCookieName);
        const auto current = token ? authService->currentSession(*token) : std::nullopt;
        if (!current) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue::list items;
            for (const auto& session : authService->listActiveSessions(current->userId)) {
                items.emplace_back(sessionJson(session, session.id == current->id));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Keeps the calling session active -- signing out "everywhere" should
    // never lock the caller out of the request they're currently making.
    CROW_ROUTE(app, "/api/v1/sessions/sign-out-others")
    .methods(crow::HTTPMethod::Post)([authService](const crow::request& request) {
        const auto token = cookieValue(request, SessionCookieName);
        const auto current = token ? authService->currentSession(*token) : std::nullopt;
        if (!current) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, current->userId)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            const int removed = authService->signOutOtherSessions(current->userId, current->id);
            crow::json::wvalue body;
            body["signedOutCount"] = removed;
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        try {
            crow::json::wvalue::list items;
            for (const auto& project : service->listProjects(resolvePrincipal(request, authService))) {
                items.emplace_back(projectJson(project));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::CreateProjectRequest create;
            create.key = requiredString(body, "key");
            create.name = requiredString(body, "name");
            create.description = optionalString(body, "description").value_or("");
            return jsonResponse(201, projectJson(service->createProject(std::move(create), *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects/<string>/archived")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body || !body.has("archived") || (body["archived"].t() != crow::json::type::True
                                                  && body["archived"].t() != crow::json::type::False)) {
                return errorResponse(400, "archived must be a boolean");
            }
            const bool archived = body["archived"].b();
            if (!service->setProjectArchived(projectKey, archived, *principal)) {
                return errorResponse(404, "Project not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Changing an active project's key (D91): the old key becomes a
    // permanent alias (project_key_aliases) and every ticket in the
    // project is renamed to the new prefix with its own old key becoming a
    // ticket_key_aliases entry -- see IDatabase::changeProjectKey for the
    // full contract.
    CROW_ROUTE(app, "/api/v1/projects/<string>/key")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto newKey = requiredString(body, "newKey");
            const auto renamed = service->changeProjectKey(projectKey, newKey, *principal);
            return renamed ? jsonResponse(200, projectJson(*renamed)) : errorResponse(404, "Project not found");
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // --- Project components (D19, KEEP_FOR_V1) ---
    CROW_ROUTE(app, "/api/v1/projects/<string>/components")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& projectKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& component : service->listComponents(projectKey, resolvePrincipal(request, authService))) {
                items.emplace_back(componentJson(component));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects/<string>/components")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::CreateComponentRequest create;
            create.projectKey = projectKey;
            create.name = requiredString(body, "name");
            create.description = optionalString(body, "description").value_or("");
            create.leadEmail = optionalString(body, "leadEmail");
            create.defaultAssigneeEmail = optionalString(body, "defaultAssigneeEmail");
            return jsonResponse(201, componentJson(service->createComponent(std::move(create), *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects/<string>/components/<string>")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& projectKey,
                                                              const std::string& componentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::EditComponentRequest edit;
            edit.name = requiredString(body, "name");
            edit.description = optionalString(body, "description").value_or("");
            edit.leadEmail = optionalString(body, "leadEmail");
            edit.defaultAssigneeEmail = optionalString(body, "defaultAssigneeEmail");
            auto component = service->editComponent(projectKey, componentId, std::move(edit), *principal);
            return component ? jsonResponse(200, componentJson(*component)) : errorResponse(404, "Component not found");
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects/<string>/components/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& projectKey,
                                                               const std::string& componentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->deleteComponent(projectKey, componentId, *principal)) {
                return errorResponse(404, "Component not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Moves a project to the recycle bin (soft delete, D88/D89) -- not a
    // permanent delete. See DELETE /api/v1/projects/<key>/permanent below.
    CROW_ROUTE(app, "/api/v1/projects/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->deleteProject(projectKey, *principal)) {
                return errorResponse(404, "Project not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects/deleted")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue::list items;
            for (const auto& project : service->listDeletedProjects(*principal)) {
                items.emplace_back(projectJson(project));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects/<string>/restore")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->restoreProject(projectKey, *principal)) {
                return errorResponse(404, "Project not found in recycle bin");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/projects/<string>/permanent")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->permanentlyDeleteProject(projectKey, *principal)) {
                return errorResponse(404, "Project not found in recycle bin");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Installation-wide anonymous read-access toggle (D59, off by default).
    CROW_ROUTE(app, "/api/v1/settings/anonymous-read")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        crow::json::wvalue body;
        body["enabled"] = service->isAnonymousReadEnabled();
        return jsonResponse(200, std::move(body));
    });

    CROW_ROUTE(app, "/api/v1/settings/anonymous-read")
    .methods(crow::HTTPMethod::Put)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body || !body.has("enabled") || (body["enabled"].t() != crow::json::type::True
                                                 && body["enabled"].t() != crow::json::type::False)) {
                return errorResponse(400, "enabled must be a boolean");
            }
            service->setAnonymousReadEnabled(body["enabled"].b(), *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // In-app admin version banner (Phase 7, D112). Global-administrator-only
    // for both read and write -- see TicketService::latestKnownVersion's
    // doc comment for why there is no automatic "check for updates"
    // mechanism (no outbound-HTTP-client infrastructure anywhere in this
    // codebase; an admin sets what they know the latest version to be).
    CROW_ROUTE(app, "/api/v1/settings/latest-known-version")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue body;
            const auto latestKnownVersion = service->latestKnownVersion(*principal);
            body["currentVersion"] = TICKETHUB_VERSION;
            body["latestKnownVersion"] = latestKnownVersion ? crow::json::wvalue(*latestKnownVersion) : crow::json::wvalue(nullptr);
            body["updateAvailable"] = latestKnownVersion.has_value() && *latestKnownVersion != TICKETHUB_VERSION;
            return jsonResponse(200, std::move(body));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/settings/latest-known-version")
    .methods(crow::HTTPMethod::Put)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            const auto version = body ? optionalString(body, "version") : std::nullopt;
            if (!version) {
                return errorResponse(400, "version must be a non-empty string");
            }
            service->setLatestKnownVersion(*version, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Kanban board WIP limits (D32/D33): a single flat, installation-wide
    // list (same read-access rule as projects/tickets), settable only by a
    // global administrator (there is no per-project board admin concept in
    // the reduced-scope model).
    CROW_ROUTE(app, "/api/v1/board-columns")([service, authService](const crow::request& request) {
        try {
            crow::json::wvalue::list items;
            for (const auto& column : service->listBoardColumns(resolvePrincipal(request, authService))) {
                crow::json::wvalue item;
                item["id"] = column.id;
                item["statusKey"] = column.statusKey;
                item["statusName"] = column.statusName;
                item["sortOrder"] = column.sortOrder;
                if (column.wipLimit) {
                    item["wipLimit"] = *column.wipLimit;
                } else {
                    item["wipLimit"] = nullptr;
                }
                items.emplace_back(std::move(item));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/board-columns/<string>")
    .methods(crow::HTTPMethod::Put)([service, authService](const crow::request& request, const std::string& statusKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            std::optional<int> wipLimit;
            if (body && body.has("wipLimit") && body["wipLimit"].t() != crow::json::type::Null) {
                if (body["wipLimit"].t() != crow::json::type::Number) {
                    return errorResponse(400, "wipLimit must be a number or null");
                }
                wipLimit = static_cast<int>(body["wipLimit"].i());
            }
            service->setBoardColumnWipLimit(statusKey, wipLimit, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Numbered/offset pagination (D126): `page` (1-based, default 1) and
    // `pageSize` (default/max Domain::MaxPageSize, D125) are both optional
    // -- a caller that sends neither gets exactly the same result set this
    // route always returned (the pre-existing, previously-undocumented
    // 200-row cap), now with `totalItems`/`totalPages` so a caller can tell
    // whether more rows exist and page through them explicitly.
    CROW_ROUTE(app, "/api/v1/tickets")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        try {
            const auto filter = ticketFilterFromQuery(request);
            const int page = optionalIntQueryParameter(request, "page").value_or(1);
            const int pageSize = optionalIntQueryParameter(request, "pageSize").value_or(Domain::DefaultPageSize);
            const auto result = service->listTicketsPaged(filter, page, pageSize, resolvePrincipal(request, authService));
            crow::json::wvalue::list items;
            for (const auto& ticket : result.items) {
                items.emplace_back(ticketJson(ticket));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            body["page"] = result.page;
            body["pageSize"] = result.pageSize;
            body["totalItems"] = result.totalItems;
            body["totalPages"] = result.totalPages();
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Read-only CSV export (D48): same filters/authorization as the JSON
    // list route above, just a different representation. Not nested under a
    // path that could collide with /api/v1/tickets/{key} since Crow resolves
    // static path segments before parameterized ones, matching the existing
    // /api/v1/tickets/deleted and /api/v1/tickets/bulk/* routes' precedent.
    CROW_ROUTE(app, "/api/v1/tickets/export.csv")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        try {
            const auto filter = ticketFilterFromQuery(request);
            const auto tickets = service->listTickets(filter, resolvePrincipal(request, authService));
            crow::response response(200, ticketsToCsv(tickets));
            response.set_header("Content-Type", "text/csv; charset=utf-8");
            response.set_header("Content-Disposition", "attachment; filename=\"tickets.csv\"");
            response.set_header("Cache-Control", "no-store");
            applySecurityHeaders(response);
            return response;
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::CreateTicketRequest create;
            create.projectKey = requiredString(body, "projectKey");
            create.summary = requiredString(body, "summary");
            create.description = optionalString(body, "description").value_or("");
            create.ticketTypeKey = optionalString(body, "ticketTypeKey").value_or("task");
            create.priorityKey = optionalString(body, "priorityKey").value_or("medium");
            create.assigneeEmail = optionalString(body, "assigneeEmail");
            create.parentTicketKey = optionalString(body, "parentTicketKey");
            create.componentName = optionalString(body, "componentName");
            create.dueDate = optionalString(body, "dueDate");
            if (body.has("storyPoints") && body["storyPoints"].t() != crow::json::type::Null) {
                create.storyPoints = body["storyPoints"].d();
            }
            if (body.has("labels") && body["labels"].t() == crow::json::type::List) {
                for (const auto& label : body["labels"]) {
                    if (label.t() == crow::json::type::String) {
                        create.labels.emplace_back(label.s());
                    }
                }
            }
            return jsonResponse(201, ticketJson(service->createTicket(std::move(create), *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/deleted")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue::list items;
            for (const auto& ticket : service->listDeletedTickets(*principal)) {
                items.emplace_back(ticketJson(ticket));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>")([service, authService](const crow::request& request, const std::string& ticketKey) {
        try {
            auto ticket = service->findTicket(ticketKey, resolvePrincipal(request, authService));
            return ticket ? jsonResponse(200, ticketJson(*ticket)) : errorResponse(404, "Ticket not found");
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Moves a ticket to the recycle bin (soft delete, D22) -- not a
    // permanent delete. See DELETE /api/v1/tickets/<key>/permanent below.
    CROW_ROUTE(app, "/api/v1/tickets/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->deleteTicket(ticketKey, *principal)) {
                return errorResponse(404, "Ticket not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/restore")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->restoreTicket(ticketKey, *principal)) {
                return errorResponse(404, "Ticket not found in recycle bin");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/permanent")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->permanentlyDeleteTicket(ticketKey, *principal)) {
                return errorResponse(404, "Ticket not found in recycle bin");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::EditTicketRequest edit;
            edit.summary = requiredString(body, "summary");
            edit.description = optionalString(body, "description").value_or("");
            edit.priorityKey = requiredString(body, "priorityKey");
            edit.assigneeEmail = optionalString(body, "assigneeEmail");
            edit.dueDate = optionalString(body, "dueDate");
            edit.ticketTypeKey = requiredString(body, "ticketTypeKey");
            edit.parentTicketKey = optionalString(body, "parentTicketKey");
            edit.componentName = optionalString(body, "componentName");
            if (body.has("storyPoints") && body["storyPoints"].t() != crow::json::type::Null) {
                edit.storyPoints = body["storyPoints"].d();
            }
            if (body.has("labels") && body["labels"].t() == crow::json::type::List) {
                for (const auto& label : body["labels"]) {
                    if (label.t() == crow::json::type::String) {
                        edit.labels.emplace_back(label.s());
                    }
                }
            }
            std::optional<std::int64_t> expectedVersion;
            if (body.has("expectedVersion") && body["expectedVersion"].t() != crow::json::type::Null) {
                expectedVersion = body["expectedVersion"].i();
            }
            auto ticket = service->editTicket(ticketKey, std::move(edit), *principal, expectedVersion);
            return ticket ? jsonResponse(200, ticketJson(*ticket)) : errorResponse(404, "Ticket not found");
        } catch (const Domain::ConcurrencyConflict& error) {
            return errorResponse(409, error.what());
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/status")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const std::string statusKey = requiredString(body, "statusKey");
            const auto resolution = optionalString(body, "resolution");
            std::optional<std::int64_t> expectedVersion;
            if (body.has("expectedVersion") && body["expectedVersion"].t() != crow::json::type::Null) {
                expectedVersion = body["expectedVersion"].i();
            }
            if (!service->changeStatus(ticketKey, statusKey, *principal, resolution, expectedVersion)) {
                return errorResponse(404, "Ticket not found");
            }
            auto ticket = service->findTicket(ticketKey, principal);
            return ticket ? jsonResponse(200, ticketJson(*ticket)) : errorResponse(404, "Ticket not found");
        } catch (const Domain::ConcurrencyConflict& error) {
            return errorResponse(409, error.what());
        } catch (const Domain::WorkflowViolation& error) {
            return errorResponse(422, error.what());
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/comments")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& ticketKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& comment : service->listComments(ticketKey, resolvePrincipal(request, authService))) {
                items.emplace_back(commentJson(comment));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/comments")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            return jsonResponse(201, commentJson(service->addComment(ticketKey, requiredString(body, "body"), *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Comment editing (D81/D83): the author may always edit their own
    // comment; otherwise the actor needs project-Admin-or-above (or global
    // admin). Sets `edited_at` -- there is no stored history of prior text.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/comments/<string>")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& ticketKey, const std::string& commentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            std::optional<std::int64_t> expectedVersion;
            if (body.has("expectedVersion") && body["expectedVersion"].t() != crow::json::type::Null) {
                expectedVersion = body["expectedVersion"].i();
            }
            auto comment = service->editComment(ticketKey, commentId, requiredString(body, "body"), *principal, expectedVersion);
            return comment ? jsonResponse(200, commentJson(*comment)) : errorResponse(404, "Comment not found");
        } catch (const Domain::ConcurrencyConflict& error) {
            return errorResponse(409, error.what());
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Tombstone delete (D82): the comment row and original body remain in
    // the database, just excluded from ordinary listing -- there is no
    // separate recycle-bin API for comments, unlike tickets and projects.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/comments/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& ticketKey, const std::string& commentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->deleteComment(ticketKey, commentId, *principal)) {
                return errorResponse(404, "Comment not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Fixed emoji reactions on comments (D84): self-service only, no
    // project-role check -- any authenticated user may react to any comment,
    // same reasoning as watch/vote. `reactionKey` is a path segment from the
    // fixed Domain::isValidCommentReactionKey set (e.g. "thumbs_up").
    CROW_ROUTE(app, "/api/v1/tickets/<string>/comments/<string>/reactions")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& ticketKey, const std::string& commentId) {
        try {
            crow::json::wvalue::list items;
            for (const auto& reaction : service->listCommentReactions(ticketKey, commentId, resolvePrincipal(request, authService))) {
                items.emplace_back(commentReactionJson(reaction));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/comments/<string>/reactions/<string>")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey, const std::string& commentId, const std::string& reactionKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            // The return value only signals whether a row was newly
            // inserted (idempotent, like watch/vote) -- an unknown ticket,
            // comment, or reaction key throws std::invalid_argument instead
            // of returning false, so there is no not-found case to check here.
            service->addCommentReaction(ticketKey, commentId, reactionKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/comments/<string>/reactions/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& ticketKey, const std::string& commentId, const std::string& reactionKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            service->removeCommentReaction(ticketKey, commentId, reactionKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Simplified worklogs (D12/D13): no own-vs-others permission split --
    // any project member (the same project-Member-or-above level as any
    // other ticket write) may edit or delete any worklog on the ticket.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/worklogs")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& ticketKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& worklog : service->listWorklogs(ticketKey, resolvePrincipal(request, authService))) {
                items.emplace_back(worklogJson(worklog));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/worklogs")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            if (!body.has("timeSpentSeconds") || body["timeSpentSeconds"].t() != crow::json::type::Number) {
                return errorResponse(400, "timeSpentSeconds must be a number");
            }
            const auto worklog = service->addWorklog(ticketKey, requiredString(body, "workDate"),
                body["timeSpentSeconds"].i(), optionalString(body, "comment"), *principal);
            return jsonResponse(201, worklogJson(worklog));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/worklogs/<string>")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& ticketKey, const std::string& worklogId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            if (!body.has("timeSpentSeconds") || body["timeSpentSeconds"].t() != crow::json::type::Number) {
                return errorResponse(400, "timeSpentSeconds must be a number");
            }
            std::optional<std::int64_t> expectedVersion;
            if (body.has("expectedVersion") && body["expectedVersion"].t() != crow::json::type::Null) {
                expectedVersion = body["expectedVersion"].i();
            }
            auto worklog = service->editWorklog(ticketKey, worklogId, requiredString(body, "workDate"),
                body["timeSpentSeconds"].i(), optionalString(body, "comment"), *principal, expectedVersion);
            return worklog ? jsonResponse(200, worklogJson(*worklog)) : errorResponse(404, "Worklog not found");
        } catch (const Domain::ConcurrencyConflict& error) {
            return errorResponse(409, error.what());
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/worklogs/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& ticketKey, const std::string& worklogId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->deleteWorklog(ticketKey, worklogId, *principal)) {
                return errorResponse(404, "Worklog not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Attachments (Phase 5, D15/D98-D105). Local filesystem storage only,
    // hardwired -- there is no storage-backend abstraction to route around.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/attachments")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& ticketKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& attachment : service->listAttachments(ticketKey, resolvePrincipal(request, authService))) {
                items.emplace_back(attachmentJson(attachment));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // multipart/form-data with a single "file" part. Fixed limits (D98, no
    // admin configuration): 25MB/file, 20 attachments/ticket, a blocked-
    // extension denylist -- all enforced in
    // Domain::validateAttachmentUpload, not here. The whole body is read
    // into memory before that check runs (no streaming/early-abort), so an
    // oversized upload is rejected only after being fully received -- an
    // accepted V1 simplification, not a decision-driven choice.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/attachments")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            crow::multipart::message multipart(request);
            const auto filePart = multipart.get_part_by_name("file");
            if (filePart.body.empty()) {
                return errorResponse(400, "A \"file\" part is required");
            }
            const auto& disposition = filePart.get_header_object("Content-Disposition");
            const auto filenameParam = disposition.params.find("filename");
            const std::string fileName = filenameParam != disposition.params.end() ? filenameParam->second : "upload";
            const auto& contentTypeHeader = filePart.get_header_object("Content-Type");
            const std::string contentType = contentTypeHeader.value.empty() ? "application/octet-stream" : contentTypeHeader.value;
            const auto attachment = service->uploadAttachment(ticketKey, fileName, contentType, filePart.body, *principal);
            return jsonResponse(201, attachmentJson(attachment));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const crow::bad_request& error) {
            return errorResponse(400, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Uploader-or-project-Admin-or-above (see TicketService::deleteAttachment
    // for why this mirrors the comment edit/delete rule).
    CROW_ROUTE(app, "/api/v1/tickets/<string>/attachments/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& ticketKey, const std::string& attachmentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->deleteAttachment(ticketKey, attachmentId, *principal)) {
                return errorResponse(404, "Attachment not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Not nested under /api/v1/tickets/{key} like the routes above: a download
    // link (and an inline <img>/<audio>/<video>/<embed> preview src) only
    // ever needs the attachment id, e.g. when rendered from an
    // `attachment://<id>` reference inside Markdown (D100) that could be
    // read from any comment on the ticket, not just its description.
    CROW_ROUTE(app, "/api/v1/attachments/<string>/download")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& attachmentId) {
        try {
            const auto [attachment, bytes] = service->downloadAttachment(attachmentId, resolvePrincipal(request, authService));
            crow::response response(200, bytes);
            response.set_header("Content-Type", sanitizeHeaderValue(attachment.contentType));
            const std::string disposition = contentTypeSafeToRenderInline(attachment.contentType) ? "inline" : "attachment";
            response.set_header("Content-Disposition", disposition + "; filename=\"" + sanitizeHeaderValue(attachment.fileName) + "\"");
            response.set_header("Cache-Control", "private, max-age=31536000, immutable");
            applySecurityHeaders(response);
            return response;
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(404, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Recycle bin (D101/D102): global-administrator-only, the same split as
    // the ticket and project recycle bins. Fixed 90-day on-demand retention
    // (checked inside TicketService::listDeletedAttachments, not here, not
    // a background job).
    CROW_ROUTE(app, "/api/v1/attachments/deleted")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue::list items;
            for (const auto& attachment : service->listDeletedAttachments(*principal)) {
                items.emplace_back(attachmentJson(attachment));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/attachments/<string>/restore")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& attachmentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            crow::json::wvalue body;
            body["ok"] = service->restoreAttachment(attachmentId, *principal);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/attachments/<string>/permanent")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& attachmentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            crow::json::wvalue body;
            body["ok"] = service->permanentlyDeleteAttachment(attachmentId, *principal);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Simple field-copy clone (D60): summary/description/type/priority/
    // labels/component into a new ticket in the same project, plus a
    // clones/is-cloned-by link back to the original.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/clone")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            return jsonResponse(201, ticketJson(service->cloneTicket(ticketKey, *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Simple integer manual ordering with renumbering (D31). `beforeTicketKey`
    // omitted or null moves the ticket to the end of its project.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/reorder")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto beforeTicketKey = optionalString(body, "beforeTicketKey");
            return jsonResponse(200, ticketJson(service->reorderTicket(ticketKey, beforeTicketKey, *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Move a ticket to a different project (D37): no compatibility check is
    // needed since every project shares the same fixed types/workflow/fields.
    // Rejected if the ticket has a parent or any children (see
    // IDatabase::moveTicket). Requires project-Member-or-above on both the
    // source and target projects.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/move")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const std::string targetProjectKey = requiredString(body, "targetProjectKey");
            return jsonResponse(200, ticketJson(service->moveTicket(ticketKey, targetProjectKey, *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/links")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& ticketKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& link : service->listTicketLinks(ticketKey, resolvePrincipal(request, authService))) {
                items.emplace_back(ticketLinkJson(link));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // The fixed link catalog (D17): blocks/relates_to/duplicates/clones.
    // Both the source (`ticketKey`) and target (`targetTicketKey`) projects
    // must be accessible to the actor, since a link touches two tickets that
    // may be in different projects.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/links")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const std::string targetTicketKey = requiredString(body, "targetTicketKey");
            const std::string linkType = requiredString(body, "linkType");
            return jsonResponse(201, ticketLinkJson(service->createTicketLink(ticketKey, targetTicketKey, linkType, *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/ticket-links/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& linkId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (!service->deleteTicketLink(linkId, *principal)) {
                return errorResponse(404, "Link not found");
            }
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Watching and voting (D20, D79): self-service only, no project-role
    // check -- any authenticated user may watch/vote on any ticket.
    CROW_ROUTE(app, "/api/v1/tickets/<string>/watchers")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& ticketKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& user : service->listWatchers(ticketKey, resolvePrincipal(request, authService))) {
                items.emplace_back(userJson(user));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/watch")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            service->watchTicket(ticketKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/watch")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            service->unwatchTicket(ticketKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/voters")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& ticketKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& user : service->listVoters(ticketKey, resolvePrincipal(request, authService))) {
                items.emplace_back(userJson(user));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/vote")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            service->voteTicket(ticketKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/<string>/vote")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& ticketKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            service->unvoteTicket(ticketKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Simple bulk actions (D36): one action kind per call, applied
    // independently per ticket -- {"succeeded": [...], "failed": [...]}
    // reports which keys went through, since a partial failure does not
    // roll back the ones that already succeeded. No cross-project move and
    // no type change in bulk (D36 explicitly excludes both).
    CROW_ROUTE(app, "/api/v1/tickets/bulk/status")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto ticketKeys = requiredTicketKeys(body);
            const std::string statusKey = requiredString(body, "statusKey");
            const auto resolution = optionalString(body, "resolution");
            return jsonResponse(200, bulkActionResultJson(service->bulkChangeStatus(ticketKeys, statusKey, resolution, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/bulk/assign")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto ticketKeys = requiredTicketKeys(body);
            const auto assigneeEmail = optionalString(body, "assigneeEmail");
            return jsonResponse(200, bulkActionResultJson(service->bulkAssign(ticketKeys, assigneeEmail, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/bulk/label")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto ticketKeys = requiredTicketKeys(body);
            const std::string label = requiredString(body, "label");
            return jsonResponse(200, bulkActionResultJson(service->bulkAddLabel(ticketKeys, label, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/tickets/bulk/delete")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            if (request.body.size() > MaxJsonRequestBodyBytes) {
                return errorResponse(413, "Request body too large");
            }
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto ticketKeys = requiredTicketKeys(body);
            return jsonResponse(200, bulkActionResultJson(service->bulkDelete(ticketKeys, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/dashboard")([service, authService](const crow::request& request) {
        try {
            const auto stats = service->dashboard(resolvePrincipal(request, authService));
            crow::json::wvalue body;
            body["totalTickets"] = stats.totalTickets;
            body["todoTickets"] = stats.todoTickets;
            body["inProgressTickets"] = stats.inProgressTickets;
            body["doneTickets"] = stats.doneTickets;
            crow::json::wvalue::list recent;
            for (const auto& ticket : stats.recentTickets) {
                recent.emplace_back(ticketJson(ticket));
            }
            body["recentTickets"] = std::move(recent);
            crow::json::wvalue::list assignedToMe;
            for (const auto& ticket : stats.assignedToMe) {
                assignedToMe.emplace_back(ticketJson(ticket));
            }
            body["assignedToMe"] = std::move(assignedToMe);
            crow::json::wvalue::list watchedTickets;
            for (const auto& ticket : stats.watchedTickets) {
                watchedTickets.emplace_back(ticketJson(ticket));
            }
            body["watchedTickets"] = std::move(watchedTickets);
            crow::json::wvalue::list upcomingDeadlines;
            for (const auto& ticket : stats.upcomingDeadlines) {
                upcomingDeadlines.emplace_back(ticketJson(ticket));
            }
            body["upcomingDeadlines"] = std::move(upcomingDeadlines);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // User directory (D80): backs @mention autocomplete. Requires a session
    // -- unlike ticket reads, this is never available to an anonymous caller
    // even when the installation-wide anonymous-read toggle is on.
    CROW_ROUTE(app, "/api/v1/users")([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue::list items;
            for (const auto& user : service->listUsers(*principal)) {
                items.emplace_back(userDirectoryJson(user));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Simple append-only admin/security audit log (D23): global-admin-only,
    // like the recycle bins. No filtering/export/pagination -- just a
    // capped, newest-first read.
    CROW_ROUTE(app, "/api/v1/admin/audit-events")([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue::list items;
            for (const auto& event : service->listAuditEvents(*principal)) {
                items.emplace_back(auditEventJson(event));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Fixed in-app notifications (D14): always scoped to the caller's own
    // notifications, never another user's.
    CROW_ROUTE(app, "/api/v1/notifications")([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            const auto unreadParam = queryParameter(request, "unread");
            const bool unreadOnly = unreadParam.has_value() && *unreadParam == "true";
            crow::json::wvalue::list items;
            for (const auto& notification : service->listNotifications(*principal, unreadOnly)) {
                items.emplace_back(notificationJson(notification));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/notifications/unread-count")([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue body;
            body["count"] = service->countUnreadNotifications(*principal);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/notifications/<string>/read")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& notificationId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            crow::json::wvalue body;
            body["ok"] = service->markNotificationRead(notificationId, *principal);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/v1/notifications/read-all")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        if (!writeRateLimitOk(request, principal)) {
            return rateLimitedResponse("Too many requests. Try again later.", 60);
        }
        try {
            crow::json::wvalue body;
            body["ok"] = service->markAllNotificationsRead(*principal);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });
}

} // namespace TicketHub::Web
