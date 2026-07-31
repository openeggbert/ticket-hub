#include "web/Api.h"

#include "domain/Errors.h"

#include <exception>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// NOTE ON VERIFICATION STATUS: this file could not be compiled in the
// authoring sandbox because outbound access to github.com (needed to fetch
// Crow via CMake FetchContent) was blocked by the sandbox's egress policy --
// the same limitation recorded in handoff/IMPLEMENTATION_STATE.md and
// docs/VERIFICATION.md for the original prototype. Everything in this file
// follows the exact patterns already used elsewhere in this file (unchanged
// helper functions, unchanged route registration style) and the session/CSRF
// design is unit-testable independently of Crow (see
// tests/identity_integration_tests.cpp for the AuthService coverage that
// backs the /api/auth/* routes below). Compile and smoke-test this file
// against a real Crow checkout before trusting it in production, per
// CLAUDE.md's build-and-test discipline.

namespace TicketHub::Web {
namespace {

crow::response jsonResponse(int status, crow::json::wvalue body) {
    crow::response response(status, body.dump());
    response.set_header("Content-Type", "application/json; charset=utf-8");
    response.set_header("Cache-Control", "no-store");
    return response;
}

crow::response errorResponse(int status, const std::string& message) {
    crow::json::wvalue body;
    body["error"] = message;
    return jsonResponse(status, std::move(body));
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
    return json;
}

crow::json::wvalue projectJson(const Domain::Project& project) {
    crow::json::wvalue json;
    json["id"] = project.id;
    json["key"] = project.key;
    json["name"] = project.name;
    json["description"] = project.description;
    json["issueCount"] = project.issueCount;
    json["openIssueCount"] = project.openIssueCount;
    if (project.lead) {
        json["lead"] = userJson(*project.lead);
    } else {
        json["lead"] = nullptr;
    }
    return json;
}

crow::json::wvalue issueJson(const Domain::Issue& issue) {
    crow::json::wvalue json;
    json["id"] = issue.id;
    json["key"] = issue.key;
    json["number"] = issue.number;
    json["projectKey"] = issue.projectKey;
    json["projectName"] = issue.projectName;
    json["summary"] = issue.summary;
    json["description"] = issue.description;
    json["type"] = crow::json::wvalue{{"key", issue.type.key},
                                      {"name", issue.type.name},
                                      {"icon", issue.type.icon},
                                      {"color", issue.type.color}};
    json["status"] = crow::json::wvalue{{"key", issue.status.key},
                                        {"name", issue.status.name},
                                        {"category", issue.status.category},
                                        {"sortOrder", issue.status.sortOrder}};
    json["priority"] = crow::json::wvalue{{"key", issue.priority.key},
                                          {"name", issue.priority.name},
                                          {"rank", issue.priority.rank},
                                          {"color", issue.priority.color}};
    json["reporter"] = userJson(issue.reporter);
    json["assignee"] = issue.assignee ? userJson(*issue.assignee) : crow::json::wvalue(nullptr);
    json["parentIssueKey"] = issue.parentIssueKey ? crow::json::wvalue(*issue.parentIssueKey) : crow::json::wvalue(nullptr);
    json["storyPoints"] = issue.storyPoints ? crow::json::wvalue(*issue.storyPoints) : crow::json::wvalue(nullptr);
    json["dueDate"] = issue.dueDate ? crow::json::wvalue(*issue.dueDate) : crow::json::wvalue(nullptr);
    json["resolution"] = issue.resolution ? crow::json::wvalue(*issue.resolution) : crow::json::wvalue(nullptr);
    crow::json::wvalue::list labels;
    for (const auto& label : issue.labels) {
        labels.emplace_back(label);
    }
    json["labels"] = std::move(labels);
    json["createdAt"] = issue.createdAt;
    json["updatedAt"] = issue.updatedAt;
    json["version"] = issue.version;
    json["rankOrder"] = issue.rankOrder;
    return json;
}

crow::json::wvalue commentJson(const Domain::Comment& comment) {
    crow::json::wvalue json;
    json["id"] = comment.id;
    json["issueId"] = comment.issueId;
    json["author"] = userJson(comment.author);
    json["body"] = comment.body;
    json["createdAt"] = comment.createdAt;
    json["updatedAt"] = comment.updatedAt;
    json["version"] = comment.version;
    json["editedAt"] = comment.editedAt ? crow::json::wvalue(*comment.editedAt) : crow::json::wvalue(nullptr);
    return json;
}

crow::json::wvalue commentReactionJson(const Domain::CommentReaction& reaction) {
    crow::json::wvalue json;
    json["reactionKey"] = reaction.reactionKey;
    json["user"] = userJson(reaction.user);
    return json;
}

crow::json::wvalue issueLinkJson(const Domain::IssueLink& link) {
    const auto labels = Domain::linkTypeLabels(link.linkType);
    crow::json::wvalue json;
    json["id"] = link.id;
    json["linkType"] = link.linkType;
    json["label"] = link.outward ? labels.outward : labels.inward;
    json["otherIssueKey"] = link.otherIssueKey;
    json["otherIssueSummary"] = link.otherIssueSummary;
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

// Simple bulk actions (D36) always take {"issueKeys": [...]} plus
// action-specific fields; every bulk route needs this.
std::vector<std::string> requiredIssueKeys(const crow::json::rvalue& body) {
    if (!body.has("issueKeys") || body["issueKeys"].t() != crow::json::type::List) {
        throw std::invalid_argument("issueKeys must be an array of strings");
    }
    std::vector<std::string> keys;
    for (const auto& item : body["issueKeys"]) {
        if (item.t() != crow::json::type::String) {
            throw std::invalid_argument("issueKeys must be an array of strings");
        }
        keys.emplace_back(item.s());
    }
    if (keys.empty()) {
        throw std::invalid_argument("issueKeys must not be empty");
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

// Resolves the caller's Principal from the session cookie. Returns nullopt
// (never throws) so route handlers can turn a missing/invalid session into a
// clean 401 response.
std::optional<Domain::Principal> resolvePrincipal(const crow::request& request,
                                                   const std::shared_ptr<Application::AuthService>& authService) {
    const auto token = cookieValue(request, SessionCookieName);
    if (!token) {
        return std::nullopt;
    }
    return authService->validateSession(*token);
}

bool csrfTokenValid(const crow::request& request) {
    const auto cookie = cookieValue(request, CsrfCookieName);
    const std::string header = request.get_header_value("X-CSRF-Token");
    return cookie.has_value() && !cookie->empty() && *cookie == header;
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

    CROW_ROUTE(app, "/api/auth/login")
    .methods(crow::HTTPMethod::Post)([authService](const crow::request& request) {
        try {
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
            // unguessable and readable solely by same-origin JS.
            addSessionCookies(response, authenticated.sessionToken, authenticated.sessionToken.substr(0, 32));
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

    CROW_ROUTE(app, "/api/auth/logout")
    .methods(crow::HTTPMethod::Post)([authService](const crow::request& request) {
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

    CROW_ROUTE(app, "/api/auth/me")([authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        return jsonResponse(200, principalJson(*principal));
    });

    CROW_ROUTE(app, "/api/projects")
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

    CROW_ROUTE(app, "/api/projects")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
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

    CROW_ROUTE(app, "/api/projects/<string>/archived")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
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

    // Moves a project to the recycle bin (soft delete, D88/D89) -- not a
    // permanent delete. See DELETE /api/projects/<key>/permanent below.
    CROW_ROUTE(app, "/api/projects/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
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

    CROW_ROUTE(app, "/api/projects/deleted")
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

    CROW_ROUTE(app, "/api/projects/<string>/restore")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
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

    CROW_ROUTE(app, "/api/projects/<string>/permanent")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& projectKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
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
    CROW_ROUTE(app, "/api/settings/anonymous-read")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        crow::json::wvalue body;
        body["enabled"] = service->isAnonymousReadEnabled();
        return jsonResponse(200, std::move(body));
    });

    CROW_ROUTE(app, "/api/settings/anonymous-read")
    .methods(crow::HTTPMethod::Put)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
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

    CROW_ROUTE(app, "/api/issues")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        try {
            Domain::IssueFilter filter;
            filter.projectKey = queryParameter(request, "project");
            filter.statusKey = queryParameter(request, "status");
            filter.search = queryParameter(request, "q");
            crow::json::wvalue::list items;
            for (const auto& issue : service->listIssues(filter, resolvePrincipal(request, authService))) {
                items.emplace_back(issueJson(issue));
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

    CROW_ROUTE(app, "/api/issues")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::CreateIssueRequest create;
            create.projectKey = requiredString(body, "projectKey");
            create.summary = requiredString(body, "summary");
            create.description = optionalString(body, "description").value_or("");
            create.issueTypeKey = optionalString(body, "issueTypeKey").value_or("task");
            create.priorityKey = optionalString(body, "priorityKey").value_or("medium");
            create.assigneeEmail = optionalString(body, "assigneeEmail");
            create.parentIssueKey = optionalString(body, "parentIssueKey");
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
            return jsonResponse(201, issueJson(service->createIssue(std::move(create), *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/deleted")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        try {
            crow::json::wvalue::list items;
            for (const auto& issue : service->listDeletedIssues(*principal)) {
                items.emplace_back(issueJson(issue));
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

    CROW_ROUTE(app, "/api/issues/<string>")([service, authService](const crow::request& request, const std::string& issueKey) {
        try {
            auto issue = service->findIssue(issueKey, resolvePrincipal(request, authService));
            return issue ? jsonResponse(200, issueJson(*issue)) : errorResponse(404, "Issue not found");
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Moves an issue to the recycle bin (soft delete, D22) -- not a
    // permanent delete. See DELETE /api/issues/<key>/permanent below.
    CROW_ROUTE(app, "/api/issues/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            if (!service->deleteIssue(issueKey, *principal)) {
                return errorResponse(404, "Issue not found");
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

    CROW_ROUTE(app, "/api/issues/<string>/restore")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            if (!service->restoreIssue(issueKey, *principal)) {
                return errorResponse(404, "Issue not found in recycle bin");
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

    CROW_ROUTE(app, "/api/issues/<string>/permanent")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            if (!service->permanentlyDeleteIssue(issueKey, *principal)) {
                return errorResponse(404, "Issue not found in recycle bin");
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

    CROW_ROUTE(app, "/api/issues/<string>")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            Domain::EditIssueRequest edit;
            edit.summary = requiredString(body, "summary");
            edit.description = optionalString(body, "description").value_or("");
            edit.priorityKey = requiredString(body, "priorityKey");
            edit.assigneeEmail = optionalString(body, "assigneeEmail");
            edit.dueDate = optionalString(body, "dueDate");
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
            auto issue = service->editIssue(issueKey, std::move(edit), *principal, expectedVersion);
            return issue ? jsonResponse(200, issueJson(*issue)) : errorResponse(404, "Issue not found");
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

    CROW_ROUTE(app, "/api/issues/<string>/status")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
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
            if (!service->changeStatus(issueKey, statusKey, *principal, resolution, expectedVersion)) {
                return errorResponse(404, "Issue not found");
            }
            auto issue = service->findIssue(issueKey, principal);
            return issue ? jsonResponse(200, issueJson(*issue)) : errorResponse(404, "Issue not found");
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

    CROW_ROUTE(app, "/api/issues/<string>/comments")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& issueKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& comment : service->listComments(issueKey, resolvePrincipal(request, authService))) {
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

    CROW_ROUTE(app, "/api/issues/<string>/comments")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            return jsonResponse(201, commentJson(service->addComment(issueKey, requiredString(body, "body"), *principal)));
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
    CROW_ROUTE(app, "/api/issues/<string>/comments/<string>")
    .methods(crow::HTTPMethod::Patch)([service, authService](const crow::request& request, const std::string& issueKey, const std::string& commentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            std::optional<std::int64_t> expectedVersion;
            if (body.has("expectedVersion") && body["expectedVersion"].t() != crow::json::type::Null) {
                expectedVersion = body["expectedVersion"].i();
            }
            auto comment = service->editComment(issueKey, commentId, requiredString(body, "body"), *principal, expectedVersion);
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
    // separate recycle-bin API for comments, unlike issues and projects.
    CROW_ROUTE(app, "/api/issues/<string>/comments/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& issueKey, const std::string& commentId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            if (!service->deleteComment(issueKey, commentId, *principal)) {
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
    CROW_ROUTE(app, "/api/issues/<string>/comments/<string>/reactions")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& issueKey, const std::string& commentId) {
        try {
            crow::json::wvalue::list items;
            for (const auto& reaction : service->listCommentReactions(issueKey, commentId, resolvePrincipal(request, authService))) {
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

    CROW_ROUTE(app, "/api/issues/<string>/comments/<string>/reactions/<string>")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey, const std::string& commentId, const std::string& reactionKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            // The return value only signals whether a row was newly
            // inserted (idempotent, like watch/vote) -- an unknown issue,
            // comment, or reaction key throws std::invalid_argument instead
            // of returning false, so there is no not-found case to check here.
            service->addCommentReaction(issueKey, commentId, reactionKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>/comments/<string>/reactions/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& issueKey, const std::string& commentId, const std::string& reactionKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            service->removeCommentReaction(issueKey, commentId, reactionKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Simple field-copy clone (D60): summary/description/type/priority/labels
    // into a new issue in the same project, plus a clones/is-cloned-by link
    // back to the original.
    CROW_ROUTE(app, "/api/issues/<string>/clone")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            return jsonResponse(201, issueJson(service->cloneIssue(issueKey, *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Simple integer manual ordering with renumbering (D31). `beforeIssueKey`
    // omitted or null moves the issue to the end of its project.
    CROW_ROUTE(app, "/api/issues/<string>/reorder")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto beforeIssueKey = optionalString(body, "beforeIssueKey");
            return jsonResponse(200, issueJson(service->reorderIssue(issueKey, beforeIssueKey, *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Move an issue to a different project (D37): no compatibility check is
    // needed since every project shares the same fixed types/workflow/fields.
    // Rejected if the issue has a parent or any children (see
    // IDatabase::moveIssue). Requires project-Member-or-above on both the
    // source and target projects.
    CROW_ROUTE(app, "/api/issues/<string>/move")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const std::string targetProjectKey = requiredString(body, "targetProjectKey");
            return jsonResponse(200, issueJson(service->moveIssue(issueKey, targetProjectKey, *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>/links")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& issueKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& link : service->listIssueLinks(issueKey, resolvePrincipal(request, authService))) {
                items.emplace_back(issueLinkJson(link));
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
    // Both the source (`issueKey`) and target (`targetIssueKey`) projects
    // must be accessible to the actor, since a link touches two issues that
    // may be in different projects.
    CROW_ROUTE(app, "/api/issues/<string>/links")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const std::string targetIssueKey = requiredString(body, "targetIssueKey");
            const std::string linkType = requiredString(body, "linkType");
            return jsonResponse(201, issueLinkJson(service->createIssueLink(issueKey, targetIssueKey, linkType, *principal)));
        } catch (const Domain::Forbidden& error) {
            return errorResponse(403, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issue-links/<string>")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& linkId) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            if (!service->deleteIssueLink(linkId, *principal)) {
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
    // check -- any authenticated user may watch/vote on any issue.
    CROW_ROUTE(app, "/api/issues/<string>/watchers")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& issueKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& user : service->listWatchers(issueKey, resolvePrincipal(request, authService))) {
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

    CROW_ROUTE(app, "/api/issues/<string>/watch")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            service->watchIssue(issueKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>/watch")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            service->unwatchIssue(issueKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>/voters")
    .methods(crow::HTTPMethod::Get)([service, authService](const crow::request& request, const std::string& issueKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& user : service->listVoters(issueKey, resolvePrincipal(request, authService))) {
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

    CROW_ROUTE(app, "/api/issues/<string>/vote")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            service->voteIssue(issueKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>/vote")
    .methods(crow::HTTPMethod::Delete)([service, authService](const crow::request& request, const std::string& issueKey) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            service->unvoteIssue(issueKey, *principal);
            crow::json::wvalue responseBody;
            responseBody["ok"] = true;
            return jsonResponse(200, std::move(responseBody));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    // Simple bulk actions (D36): one action kind per call, applied
    // independently per issue -- {"succeeded": [...], "failed": [...]}
    // reports which keys went through, since a partial failure does not
    // roll back the ones that already succeeded. No cross-project move and
    // no type change in bulk (D36 explicitly excludes both).
    CROW_ROUTE(app, "/api/issues/bulk/status")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto issueKeys = requiredIssueKeys(body);
            const std::string statusKey = requiredString(body, "statusKey");
            const auto resolution = optionalString(body, "resolution");
            return jsonResponse(200, bulkActionResultJson(service->bulkChangeStatus(issueKeys, statusKey, resolution, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/bulk/assign")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto issueKeys = requiredIssueKeys(body);
            const auto assigneeEmail = optionalString(body, "assigneeEmail");
            return jsonResponse(200, bulkActionResultJson(service->bulkAssign(issueKeys, assigneeEmail, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/bulk/label")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto issueKeys = requiredIssueKeys(body);
            const std::string label = requiredString(body, "label");
            return jsonResponse(200, bulkActionResultJson(service->bulkAddLabel(issueKeys, label, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/bulk/delete")
    .methods(crow::HTTPMethod::Post)([service, authService](const crow::request& request) {
        const auto principal = resolvePrincipal(request, authService);
        if (!principal) {
            return errorResponse(401, "Not authenticated");
        }
        if (!csrfTokenValid(request)) {
            return errorResponse(403, "Missing or invalid CSRF token");
        }
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const auto issueKeys = requiredIssueKeys(body);
            return jsonResponse(200, bulkActionResultJson(service->bulkDelete(issueKeys, *principal)));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/dashboard")([service, authService](const crow::request& request) {
        try {
            const auto stats = service->dashboard(resolvePrincipal(request, authService));
            crow::json::wvalue body;
            body["totalIssues"] = stats.totalIssues;
            body["todoIssues"] = stats.todoIssues;
            body["inProgressIssues"] = stats.inProgressIssues;
            body["doneIssues"] = stats.doneIssues;
            crow::json::wvalue::list recent;
            for (const auto& issue : stats.recentIssues) {
                recent.emplace_back(issueJson(issue));
            }
            body["recentIssues"] = std::move(recent);
            return jsonResponse(200, std::move(body));
        } catch (const Domain::AuthenticationRequired& error) {
            return errorResponse(401, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });
}

} // namespace TicketHub::Web
