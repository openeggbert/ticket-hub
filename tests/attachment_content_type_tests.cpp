// Attachment inline-vs-download decision (security audit 2026-08-26, C2).
//
// The original implementation compared an attacker-controlled `Content-Type`
// against lowercase deny-list prefixes with a case-sensitive `rfind`. MIME
// types are case-insensitive (RFC 2045 section 5.1), so `TEXT/HTML` matched
// nothing, was served `Content-Disposition: inline`, and executed script in
// the application's own origin -- confirmed live in headless Chromium before
// the fix. Every bypass reproduced in that audit is pinned below.

#include "web/AttachmentContentType.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void requireBlocked(const std::string& contentType) {
    require(!TicketHub::Web::isInlineSafeContentType(contentType),
            "\"" + contentType + "\" must never be rendered inline");
}

void requireInline(const std::string& contentType) {
    require(TicketHub::Web::isInlineSafeContentType(contentType),
            "\"" + contentType + "\" should be safe to render inline");
}

} // namespace

int main() {
    using namespace TicketHub::Web;

    // --- normalizeContentType ---
    require(normalizeContentType("TEXT/HTML") == "text/html", "normalize lowercases");
    require(normalizeContentType("text/plain; charset=utf-8") == "text/plain", "normalize drops parameters");
    require(normalizeContentType("  Text/Plain  ") == "text/plain", "normalize trims surrounding whitespace");
    require(normalizeContentType("\tIMAGE/PNG;q=1") == "image/png", "normalize handles tabs and parameters");
    require(normalizeContentType("").empty(), "normalize returns empty for empty input");
    require(normalizeContentType("   ").empty(), "normalize returns empty for whitespace-only input");
    require(normalizeContentType("; charset=utf-8").empty(), "normalize returns empty when there is no type");

    // --- The exact bypasses reproduced during the audit ---
    requireBlocked("TEXT/HTML");
    requireBlocked("Text/Html;charset=utf-8");
    requireBlocked("image/SVG+XML");
    requireBlocked(" text/html");

    // --- Scriptable document types, in every casing ---
    for (const auto& dangerous : {"text/html", "TEXT/HTML", "Text/HTML", "application/xhtml+xml",
                                  "APPLICATION/XHTML+XML", "image/svg+xml", "IMAGE/SVG+XML",
                                  "application/xml", "text/xml", "application/xslt+xml",
                                  "application/javascript", "text/javascript", "application/ecmascript",
                                  "application/rdf+xml", "application/mathml+xml", "text/html-sandboxed"}) {
        requireBlocked(dangerous);
    }

    // --- An allow-list closes the open end a deny-list left: anything
    // unrecognized is a download, including types nobody thought about. ---
    requireBlocked("application/octet-stream");
    requireBlocked("application/zip");
    requireBlocked("text/vtt");
    requireBlocked("font/woff2");
    requireBlocked("");
    requireBlocked("nonsense");
    requireBlocked("text/");
    // Prefix matching must require something after the slash, and must not
    // match a type that merely starts with the same letters.
    requireBlocked("audio/");
    requireBlocked("video/");
    requireBlocked("videographics/x-thing");

    // --- Types that genuinely cannot script when rendered directly ---
    for (const auto& safe : {"image/png", "image/jpeg", "image/gif", "image/webp", "image/bmp",
                             "image/avif", "application/pdf", "text/plain", "text/csv",
                             "audio/mpeg", "video/mp4", "video/webm"}) {
        requireInline(safe);
    }
    requireInline("IMAGE/PNG");
    requireInline("Application/PDF");
    requireInline("text/plain; charset=utf-8");
    requireInline("AUDIO/OGG");

    // --- sanitizeHeaderValue: no response splitting, no quote escape ---
    require(sanitizeHeaderValue("report.pdf") == "report.pdf", "an ordinary file name is unchanged");
    require(sanitizeHeaderValue("a\r\nX-Injected: 1") == "aX-Injected: 1", "CR and LF are stripped");
    require(sanitizeHeaderValue("a\"; x=\"b") == "a; x=b", "double quotes are stripped");
    require(sanitizeHeaderValue("").empty(), "empty stays empty");

    std::cout << "attachment content-type tests passed\n";
    return 0;
}
