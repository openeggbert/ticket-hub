#include "web/Api.h"

#include "domain/Errors.h"

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    json["username"] = user.username;
    json["displayName"] = user.displayName;
    json["email"] = user.email;
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
    crow::json::wvalue::list labels;
    for (const auto& label : issue.labels) {
        labels.emplace_back(label);
    }
    json["labels"] = std::move(labels);
    json["createdAt"] = issue.createdAt;
    json["updatedAt"] = issue.updatedAt;
    json["version"] = issue.version;
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
    return json;
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

} // namespace

void registerApiRoutes(crow::SimpleApp& app,
                       const std::shared_ptr<Application::TicketService>& service) {
    CROW_ROUTE(app, "/api/health")([service] {
        crow::json::wvalue body;
        body["status"] = "ok";
        body["service"] = "ticket-hub";
        body["version"] = TICKETHUB_VERSION;
        body["database"] = service->backendName();
        return jsonResponse(200, std::move(body));
    });

    CROW_ROUTE(app, "/api/projects")([service] {
        try {
            crow::json::wvalue::list items;
            for (const auto& project : service->listProjects()) {
                items.emplace_back(projectJson(project));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues")
    .methods(crow::HTTPMethod::Get)([service](const crow::request& request) {
        try {
            Domain::IssueFilter filter;
            filter.projectKey = queryParameter(request, "project");
            filter.statusKey = queryParameter(request, "status");
            filter.search = queryParameter(request, "q");
            crow::json::wvalue::list items;
            for (const auto& issue : service->listIssues(filter)) {
                items.emplace_back(issueJson(issue));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues")
    .methods(crow::HTTPMethod::Post)([service](const crow::request& request) {
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
            create.assigneeUsername = optionalString(body, "assigneeUsername");
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
            return jsonResponse(201, issueJson(service->createIssue(std::move(create))));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>")([service](const std::string& issueKey) {
        try {
            auto issue = service->findIssue(issueKey);
            return issue ? jsonResponse(200, issueJson(*issue)) : errorResponse(404, "Issue not found");
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>/status")
    .methods(crow::HTTPMethod::Patch)([service](const crow::request& request, const std::string& issueKey) {
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            const std::string statusKey = requiredString(body, "statusKey");
            std::optional<std::int64_t> expectedVersion;
            if (body.has("expectedVersion") && body["expectedVersion"].t() != crow::json::type::Null) {
                expectedVersion = body["expectedVersion"].i();
            }
            if (!service->changeStatus(issueKey, statusKey, expectedVersion)) {
                return errorResponse(404, "Issue not found");
            }
            auto issue = service->findIssue(issueKey);
            return issue ? jsonResponse(200, issueJson(*issue)) : errorResponse(404, "Issue not found");
        } catch (const Domain::ConcurrencyConflict& error) {
            return errorResponse(409, error.what());
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>/comments")
    .methods(crow::HTTPMethod::Get)([service](const std::string& issueKey) {
        try {
            crow::json::wvalue::list items;
            for (const auto& comment : service->listComments(issueKey)) {
                items.emplace_back(commentJson(comment));
            }
            crow::json::wvalue body;
            body["items"] = std::move(items);
            return jsonResponse(200, std::move(body));
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/issues/<string>/comments")
    .methods(crow::HTTPMethod::Post)([service](const crow::request& request, const std::string& issueKey) {
        try {
            const auto body = crow::json::load(request.body);
            if (!body) {
                return errorResponse(400, "Request body must be valid JSON");
            }
            return jsonResponse(201, commentJson(service->addComment(issueKey, requiredString(body, "body"))));
        } catch (const std::invalid_argument& error) {
            return errorResponse(400, error.what());
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });

    CROW_ROUTE(app, "/api/dashboard")([service] {
        try {
            const auto stats = service->dashboard();
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
        } catch (const std::exception& error) {
            return errorResponse(500, error.what());
        }
    });
}

} // namespace TicketHub::Web
