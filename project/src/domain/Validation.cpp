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

} // namespace TicketHub::Domain
