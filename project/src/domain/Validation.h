#pragma once

#include "domain/Models.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TicketHub::Domain {

std::string normalizeProjectKey(const std::string& value);
std::string normalizeTicketKey(const std::string& value);
std::string normalizeLabel(const std::string& value);
bool isValidProjectKey(const std::string& value);
bool isValidTicketKey(const std::string& value);
std::vector<std::string> validateCreateTicket(const CreateTicketRequest& request);
std::vector<std::string> validateEditTicket(const EditTicketRequest& request);

std::string normalizeEmail(const std::string& value);
bool isValidEmail(const std::string& value);
// D56/D80: lowercase, 1-32 characters, letters/digits/underscore only.
std::string normalizeHandle(const std::string& value);
bool isValidHandle(const std::string& value);
// Baseline strength check only (minimum length, not identical to the
// account's own email/display name). Full lockout/rate-limiting policy is a
// later phase; see docs/REDUCED_SCOPE_ROADMAP.md Phase 1/6.
std::vector<std::string> validatePassword(const std::string& password,
                                          const std::string& email,
                                          const std::string& displayName);
std::vector<std::string> validateCreateUser(const CreateUserRequest& request);

std::vector<std::string> validateCreateProject(const CreateProjectRequest& request);

std::vector<std::string> validateAddWorklog(const AddWorklogRequest& request);
std::vector<std::string> validateEditWorklog(const EditWorklogRequest& request);

// Fixed attachment limits (D98): no admin configuration, no quotas.
constexpr std::int64_t AttachmentMaxBytes = 25 * 1024 * 1024;
constexpr int AttachmentMaxPerTicket = 20;
std::vector<std::string> validateAttachmentUpload(const std::string& fileName,
                                                  std::int64_t byteSize,
                                                  int existingAttachmentCount);

} // namespace TicketHub::Domain
