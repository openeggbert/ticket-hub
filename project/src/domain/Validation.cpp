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

} // namespace

std::string normalizeProjectKey(const std::string& value) {
    return upper(trim(value));
}

std::string normalizeIssueKey(const std::string& value) {
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

bool isValidIssueKey(const std::string& value) {
    static const std::regex pattern("^[A-Z][A-Z0-9]{1,11}-[1-9][0-9]*$");
    return std::regex_match(normalizeIssueKey(value), pattern);
}

std::vector<std::string> validateCreateIssue(const CreateIssueRequest& request) {
    std::vector<std::string> errors;
    const auto normalizedKey = normalizeProjectKey(request.projectKey);

    if (!isValidProjectKey(normalizedKey)) {
        errors.emplace_back("projectKey must contain 2-12 uppercase letters or digits and start with a letter");
    }
    if (request.summary.empty()) {
        errors.emplace_back("summary is required");
    } else if (request.summary.size() > 255) {
        errors.emplace_back("summary must not exceed 255 characters");
    }
    if (request.description.size() > 100000) {
        errors.emplace_back("description must not exceed 100000 characters");
    }
    if (request.issueTypeKey.empty()) {
        errors.emplace_back("issueTypeKey is required");
    }
    if (request.priorityKey.empty()) {
        errors.emplace_back("priorityKey is required");
    }
    if (request.storyPoints.has_value() && (*request.storyPoints < 0.0 || *request.storyPoints > 10000.0)) {
        errors.emplace_back("storyPoints must be between 0 and 10000");
    }
    if (request.labels.size() > 50) {
        errors.emplace_back("an issue may have at most 50 labels");
    }
    if (std::any_of(request.labels.begin(), request.labels.end(), [](const std::string& label) {
            const auto normalized = normalizeLabel(label);
            return normalized.empty() || normalized.size() > 64;
        })) {
        errors.emplace_back("labels must contain 1-64 characters after trimming");
    }

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
    return errors;
}

} // namespace TicketHub::Domain
