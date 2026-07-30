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

} // namespace TicketHub::Domain
