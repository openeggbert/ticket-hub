#include "web/AttachmentContentType.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace TicketHub::Web {
namespace {

// Types a browser renders without ever giving the content a scripting
// context. Anything not listed here is served as a forced download, so a new
// format is a deliberate addition rather than a silent omission.
constexpr std::array<std::string_view, 13> InlineSafeExactTypes = {
    "image/png", "image/jpeg", "image/gif",  "image/webp", "image/bmp",
    "image/avif", "image/apng", "image/tiff", "image/x-icon", "image/vnd.microsoft.icon",
    // A PDF is rendered by the browser's own viewer, which has no access to
    // the embedding origin's DOM or cookies; the app additionally frames its
    // preview with `sandbox=""` (D99).
    "application/pdf",
    "text/plain", "text/csv",
};

// Audio and video subtypes are too numerous to enumerate, and none of them
// can execute script in a document context. Note `image/` is deliberately
// NOT a prefix here: `image/svg+xml` is a scriptable XML document.
constexpr std::array<std::string_view, 2> InlineSafePrefixes = {"audio/", "video/"};

char lower(const unsigned char character) {
    return static_cast<char>(std::tolower(character));
}

} // namespace

std::string normalizeContentType(const std::string& rawContentType) {
    // Everything from the first `;` onward is a parameter list
    // (`text/plain; charset=utf-8`), not part of the type itself.
    std::string value = rawContentType.substr(0, rawContentType.find(';'));

    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t");
    value = value.substr(first, last - first + 1);

    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) { return lower(character); });
    return value;
}

bool isInlineSafeContentType(const std::string& rawContentType) {
    const std::string normalized = normalizeContentType(rawContentType);
    if (normalized.empty()) {
        return false;
    }
    for (const auto& safe : InlineSafeExactTypes) {
        if (normalized == safe) {
            return true;
        }
    }
    for (const auto& prefix : InlineSafePrefixes) {
        if (normalized.size() > prefix.size() && normalized.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

std::string sanitizeHeaderValue(const std::string& value) {
    std::string sanitized = value;
    sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(),
                                   [](const unsigned char character) {
                                       return character == '\r' || character == '\n' || character == '"';
                                   }),
                    sanitized.end());
    return sanitized;
}

} // namespace TicketHub::Web
