#include "domain/Validation.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace TicketHub::Domain {
namespace {

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::string upper(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return result;
}

// Shared between validateCreateTicket and validateEditTicket: the standard
// content fields both requests carry (summary/description/priorityKey/
// storyPoints/labels). projectKey/ticketTypeKey (create-only) and the
// hierarchy/parentTicketKey rules (checked against database state, not pure
// input validation) are each caller's own responsibility.
void appendTicketContentErrors(std::vector<std::string>& errors,
                              const std::string& summary,
                              const std::string& description,
                              const std::string& priorityKey,
                              const std::optional<double>& storyPoints,
                              const std::vector<std::string>& labels) {
    if (summary.empty()) {
        errors.emplace_back("summary is required");
    } else if (summary.size() > 255) {
        errors.emplace_back("summary must not exceed 255 characters");
    }
    if (description.size() > 100000) {
        errors.emplace_back("description must not exceed 100000 characters");
    }
    if (priorityKey.empty()) {
        errors.emplace_back("priorityKey is required");
    }
    if (storyPoints.has_value() && (*storyPoints < 0.0 || *storyPoints > 10000.0)) {
        errors.emplace_back("storyPoints must be between 0 and 10000");
    }
    if (labels.size() > 50) {
        errors.emplace_back("a ticket may have at most 50 labels");
    }
    if (std::any_of(labels.begin(), labels.end(), [](const std::string& label) {
            const auto normalized = normalizeLabel(label);
            return normalized.empty() || normalized.size() > 64;
        })) {
        errors.emplace_back("labels must contain 1-64 characters after trimming");
    }
}

} // namespace

std::string normalizeProjectKey(const std::string& value) {
    return upper(trim(value));
}

std::string normalizeTicketKey(const std::string& value) {
    return upper(trim(value));
}

std::string normalizeLabel(const std::string& value) {
    std::string result = trim(value);
    std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool isValidProjectKey(const std::string& value) {
    static const std::regex pattern("^[A-Z][A-Z0-9]{1,11}$");
    return std::regex_match(normalizeProjectKey(value), pattern);
}

bool isValidTicketKey(const std::string& value) {
    static const std::regex pattern("^[A-Z][A-Z0-9]{1,11}-[1-9][0-9]*$");
    return std::regex_match(normalizeTicketKey(value), pattern);
}

std::vector<std::string> validateCreateTicket(const CreateTicketRequest& request) {
    std::vector<std::string> errors;
    const auto normalizedKey = normalizeProjectKey(request.projectKey);

    if (!isValidProjectKey(normalizedKey)) {
        errors.emplace_back("projectKey must contain 2-12 uppercase letters or digits and start with a letter");
    }
    if (request.ticketTypeKey.empty()) {
        errors.emplace_back("ticketTypeKey is required");
    }
    appendTicketContentErrors(errors, request.summary, request.description, request.priorityKey,
                             request.storyPoints, request.labels);

    return errors;
}

std::vector<std::string> validateEditTicket(const EditTicketRequest& request) {
    std::vector<std::string> errors;
    if (request.ticketTypeKey.empty()) {
        errors.emplace_back("ticketTypeKey is required");
    }
    appendTicketContentErrors(errors, request.summary, request.description, request.priorityKey,
                             request.storyPoints, request.labels);
    return errors;
}

std::string normalizeEmail(const std::string& value) {
    std::string result = trim(value);
    std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool isValidEmail(const std::string& value) {
    // Deliberately conservative rather than RFC 5322-complete: this only
    // needs to catch obviously-wrong input, since the real check that
    // matters is uniqueness plus the user actually being able to receive
    // administrator communication out of band (there is no verification
    // email in V1 -- see docs/REMOVED_AND_DEFERRED_FEATURES.md).
    static const std::regex pattern(R"(^[^\s@]+@[^\s@]+\.[^\s@]{2,}$)");
    const auto normalized = normalizeEmail(value);
    return normalized.size() <= 320 && std::regex_match(normalized, pattern);
}

std::string normalizeHandle(const std::string& value) {
    std::string result = trim(value);
    std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool isValidHandle(const std::string& value) {
    static const std::regex pattern("^[a-z0-9_]{1,32}$");
    return std::regex_match(normalizeHandle(value), pattern);
}

std::vector<std::string> validatePassword(const std::string& password,
                                          const std::string& email,
                                          const std::string& displayName) {
    std::vector<std::string> errors;
    if (password.size() < 10) {
        errors.emplace_back("password must be at least 10 characters");
    }
    if (password.size() > 256) {
        errors.emplace_back("password must not exceed 256 characters");
    }
    const auto normalizedPassword = normalizeEmail(password);
    if (!email.empty() && normalizedPassword == normalizeEmail(email)) {
        errors.emplace_back("password must not be the same as the email address");
    }
    if (!displayName.empty() && normalizedPassword == normalizeEmail(displayName)) {
        errors.emplace_back("password must not be the same as the display name");
    }
    return errors;
}

std::vector<std::string> validateCreateUser(const CreateUserRequest& request) {
    std::vector<std::string> errors;
    if (!isValidEmail(request.email)) {
        errors.emplace_back("email must be a valid address");
    }
    if (request.displayName.empty()) {
        errors.emplace_back("displayName is required");
    } else if (request.displayName.size() > 160) {
        errors.emplace_back("displayName must not exceed 160 characters");
    }
    const auto passwordErrors = validatePassword(request.password, request.email, request.displayName);
    errors.insert(errors.end(), passwordErrors.begin(), passwordErrors.end());
    if (request.handle && !isValidHandle(*request.handle)) {
        errors.emplace_back("handle must be 1-32 lowercase letters, digits, or underscores");
    }
    return errors;
}

bool isValidClockFormat(const std::string& value) {
    return value == "12h" || value == "24h";
}

std::vector<std::string> validateUpdatePreferences(const UpdatePreferencesRequest& request) {
    std::vector<std::string> errors;
    if (trim(request.timeZone).empty()) {
        errors.emplace_back("timeZone is required");
    } else if (request.timeZone.size() > 80) {
        errors.emplace_back("timeZone must not exceed 80 characters");
    }
    if (!isValidClockFormat(request.clockFormat)) {
        errors.emplace_back("clockFormat must be \"12h\" or \"24h\"");
    }
    return errors;
}

std::vector<std::string> validateCreateProject(const CreateProjectRequest& request) {
    std::vector<std::string> errors;
    if (!isValidProjectKey(normalizeProjectKey(request.key))) {
        errors.emplace_back("key must contain 2-12 uppercase letters or digits and start with a letter");
    }
    if (request.name.empty()) {
        errors.emplace_back("name is required");
    } else if (request.name.size() > 160) {
        errors.emplace_back("name must not exceed 160 characters");
    }
    if (request.description.size() > 10000) {
        errors.emplace_back("description must not exceed 10000 characters");
    }
    return errors;
}

namespace {
void appendComponentContentErrors(std::vector<std::string>& errors,
                                  const std::string& name,
                                  const std::string& description) {
    if (name.empty()) {
        errors.emplace_back("name is required");
    } else if (name.size() > 160) {
        errors.emplace_back("name must not exceed 160 characters");
    }
    if (description.size() > 10000) {
        errors.emplace_back("description must not exceed 10000 characters");
    }
}
} // namespace

std::vector<std::string> validateCreateComponent(const CreateComponentRequest& request) {
    std::vector<std::string> errors;
    if (!isValidProjectKey(normalizeProjectKey(request.projectKey))) {
        errors.emplace_back("projectKey must contain 2-12 uppercase letters or digits and start with a letter");
    }
    appendComponentContentErrors(errors, request.name, request.description);
    return errors;
}

std::vector<std::string> validateEditComponent(const EditComponentRequest& request) {
    std::vector<std::string> errors;
    appendComponentContentErrors(errors, request.name, request.description);
    return errors;
}

namespace {
bool isValidCustomFieldType(const std::string& fieldType) {
    return fieldType == "text" || fieldType == "number" || fieldType == "date" ||
           fieldType == "checkbox" || fieldType == "single_select" || fieldType == "multi_select";
}

bool isSelectFieldType(const std::string& fieldType) {
    return fieldType == "single_select" || fieldType == "multi_select";
}

void appendCustomFieldContentErrors(std::vector<std::string>& errors,
                                     const std::string& name,
                                     const std::string& fieldType,
                                     const std::vector<std::string>& options) {
    if (name.empty()) {
        errors.emplace_back("name is required");
    } else if (name.size() > 160) {
        errors.emplace_back("name must not exceed 160 characters");
    }
    if (!isValidCustomFieldType(fieldType)) {
        errors.emplace_back("fieldType must be one of text, number, date, checkbox, single_select, multi_select");
        return;
    }
    if (isSelectFieldType(fieldType)) {
        if (options.empty()) {
            errors.emplace_back("options must contain at least one value for a select field");
        }
        for (const auto& option : options) {
            if (option.empty() || option.size() > 160) {
                errors.emplace_back("each option must be 1-160 characters");
                break;
            }
        }
    } else if (!options.empty()) {
        errors.emplace_back("options is only meaningful for single_select/multi_select fields");
    }
}
} // namespace

std::vector<std::string> validateCreateCustomField(const CreateCustomFieldRequest& request) {
    std::vector<std::string> errors;
    if (!isValidProjectKey(normalizeProjectKey(request.projectKey))) {
        errors.emplace_back("projectKey must contain 2-12 uppercase letters or digits and start with a letter");
    }
    appendCustomFieldContentErrors(errors, request.name, request.fieldType, request.options);
    return errors;
}

std::vector<std::string> validateEditCustomField(const EditCustomFieldRequest& request) {
    std::vector<std::string> errors;
    // fieldType is not editable (changing it would strand every existing
    // stored value's meaning -- e.g. a single_select's options no longer
    // matching a previously-recorded value), so this only re-validates
    // name/options against the field's own existing fieldType, which the
    // caller (TicketService) already knows and re-supplies via the
    // existing options bound.
    if (request.name.empty()) {
        errors.emplace_back("name is required");
    } else if (request.name.size() > 160) {
        errors.emplace_back("name must not exceed 160 characters");
    }
    for (const auto& option : request.options) {
        if (option.empty() || option.size() > 160) {
            errors.emplace_back("each option must be 1-160 characters");
            break;
        }
    }
    return errors;
}

namespace {
// D12/D13: no time-estimate linkage, so the only bound on timeSpentSeconds
// is sanity -- reject zero/negative (meaningless) and an implausibly large
// single entry (1000 hours), not a business rule.
constexpr std::int64_t MaxWorklogTimeSpentSeconds = 1000LL * 3600;

void appendWorklogContentErrors(std::vector<std::string>& errors,
                                const std::string& workDate,
                                const std::int64_t timeSpentSeconds,
                                const std::optional<std::string>& comment) {
    if (workDate.empty()) {
        errors.emplace_back("workDate is required");
    }
    if (timeSpentSeconds <= 0) {
        errors.emplace_back("timeSpentSeconds must be greater than zero");
    } else if (timeSpentSeconds > MaxWorklogTimeSpentSeconds) {
        errors.emplace_back("timeSpentSeconds must not exceed 3600000 (1000 hours)");
    }
    if (comment && comment->size() > 10000) {
        errors.emplace_back("comment must not exceed 10000 characters");
    }
}
} // namespace

std::vector<std::string> validateAddWorklog(const AddWorklogRequest& request) {
    std::vector<std::string> errors;
    appendWorklogContentErrors(errors, request.workDate, request.timeSpentSeconds, request.comment);
    return errors;
}

std::vector<std::string> validateEditWorklog(const EditWorklogRequest& request) {
    std::vector<std::string> errors;
    appendWorklogContentErrors(errors, request.workDate, request.timeSpentSeconds, request.comment);
    return errors;
}

namespace {

// D98: "blocked dangerous extensions" -- a denylist, not a MIME allow-list
// (the original decision's allow/deny/quota configuration subsystem was
// simplified away entirely). Chosen as a conservative, common set of
// directly-executable/script file types; not exhaustive antivirus-grade
// filtering, which D98 explicitly excludes.
const std::vector<std::string>& blockedAttachmentExtensions() {
    static const std::vector<std::string> extensions = {
        "exe", "bat", "cmd", "com", "scr", "msi", "msp", "dll", "ps1", "psm1",
        "vbs", "vbe", "js", "jse", "wsf", "wsh", "jar", "app", "apk", "sh",
        "bin", "cpl", "gadget", "hta", "lnk", "reg", "vb", "ws", "action",
    };
    return extensions;
}

std::string lowerExtension(const std::string& fileName) {
    const auto dot = fileName.find_last_of('.');
    if (dot == std::string::npos || dot + 1 == fileName.size()) {
        return {};
    }
    std::string extension = fileName.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

} // namespace

std::vector<std::string> validateAttachmentUpload(const std::string& fileName,
                                                  const std::int64_t byteSize,
                                                  const int existingAttachmentCount) {
    std::vector<std::string> errors;
    if (trim(fileName).empty()) {
        errors.emplace_back("File name is required");
    }
    if (byteSize <= 0) {
        errors.emplace_back("File must not be empty");
    }
    if (byteSize > AttachmentMaxBytes) {
        errors.emplace_back("File exceeds the maximum attachment size of 25MB");
    }
    if (existingAttachmentCount >= AttachmentMaxPerTicket) {
        errors.emplace_back("This ticket already has the maximum of 20 attachments");
    }
    const auto extension = lowerExtension(fileName);
    if (!extension.empty()) {
        const auto& blocked = blockedAttachmentExtensions();
        if (std::find(blocked.begin(), blocked.end(), extension) != blocked.end()) {
            errors.emplace_back("File type ." + extension + " is not allowed");
        }
    }
    return errors;
}

} // namespace TicketHub::Domain
