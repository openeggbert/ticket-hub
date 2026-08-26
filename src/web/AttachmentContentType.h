#pragma once

#include <string>

namespace TicketHub::Web {

// Attachment response-header safety (security audit 2026-08-26, finding C2).
//
// Kept deliberately free of any Crow dependency so it can be unit-tested
// standalone, the same arrangement `RateLimiter` already uses.
//
// D98 has no upload-time MIME allow-list, so `attachments.content_type` is
// whatever the uploading client declared -- fully attacker-controlled. The
// download route echoes it back, so the decision of whether a browser may
// render that response *as a document* is the entire boundary between "a
// file someone attached" and "script running in this installation's origin".

// Lowercases, trims, and drops any `; charset=...`-style parameters, so a
// caller compares against a canonical `type/subtype`. Returns an empty
// string for input with no type at all.
std::string normalizeContentType(const std::string& rawContentType);

// True only for content types a browser cannot execute script from when it
// renders the response directly.
//
// This is an allow-list on purpose. The original implementation was a
// deny-list of dangerous prefixes (`text/html`, `image/svg`, ...) compared
// case-sensitively, but MIME types are case-insensitive (RFC 2045 section
// 5.1) -- so `TEXT/HTML` matched nothing, was served `Content-Disposition:
// inline`, and executed script in this application's origin. A deny-list of
// dangerous types has no closed end; the set of types that are genuinely
// safe to render does.
bool isInlineSafeContentType(const std::string& rawContentType);

// Strips CR, LF and double quotes so a caller-supplied file name or content
// type cannot inject an extra response header or break out of the quoted
// `filename="..."` parameter.
std::string sanitizeHeaderValue(const std::string& value);

} // namespace TicketHub::Web
