#pragma once

#include "domain/Models.h"

#include <string>
#include <vector>

namespace TicketHub::Domain {

std::string normalizeProjectKey(const std::string& value);
std::string normalizeIssueKey(const std::string& value);
std::string normalizeLabel(const std::string& value);
bool isValidProjectKey(const std::string& value);
bool isValidIssueKey(const std::string& value);
std::vector<std::string> validateCreateIssue(const CreateIssueRequest& request);

std::string normalizeEmail(const std::string& value);
bool isValidEmail(const std::string& value);
// Baseline strength check only (minimum length, not identical to the
// account's own email/display name). Full lockout/rate-limiting policy is a
// later phase; see docs/REDUCED_SCOPE_ROADMAP.md Phase 1/6.
std::vector<std::string> validatePassword(const std::string& password,
                                          const std::string& email,
                                          const std::string& displayName);
std::vector<std::string> validateCreateUser(const CreateUserRequest& request);

std::vector<std::string> validateCreateProject(const CreateProjectRequest& request);

} // namespace TicketHub::Domain
