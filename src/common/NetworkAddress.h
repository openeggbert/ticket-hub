#pragma once

#include <optional>
#include <string>

namespace TicketHub::Common {

// Outbound-target address checks (security audit 2026-08-26, finding M5).
//
// `validateCreateWebhookSubscription` already requires an `http://` or
// `https://` prefix, which rules out `file://` and `gopher://`, and
// `WebhookDeliveryClient` refuses to follow redirects. What was missing is
// any check on *where* the target resolves: a webhook could name
// `127.0.0.1`, RFC 1918 space, or a cloud metadata endpoint, and the outbox
// worker would dutifully make that request from inside the deployment's own
// network. Only the HTTP status is recorded, never the body, so this is a
// blind SSRF -- and it is reachable only by a global administrator, which is
// why it is a hardening fix rather than a privilege-boundary bug.

// Extracts the host from an `http://`/`https://` URL: no scheme, no
// userinfo, no port, no brackets around an IPv6 literal. Returns nullopt if
// the URL is not http(s) or has no host at all.
std::optional<std::string> hostFromHttpUrl(const std::string& url);

// True if `text` parses as an IPv4 or IPv6 literal that must never be an
// outbound target: loopback, private/unique-local, link-local (including
// the 169.254.169.254 cloud metadata address), CGNAT, multicast, benchmark
// and reserved ranges, and the unspecified address. False for anything that
// is not an IP literal at all -- callers resolve those first.
bool isBlockedIpLiteral(const std::string& text);

// Resolves `host` and returns true if it is an IP literal in a blocked
// range, or if *any* address it resolves to is. Returns true when resolution
// fails outright: a target this process cannot resolve cannot be delivered
// to either, so refusing at creation time gives a clear error instead of a
// delivery row that can only ever fail.
//
// This is a check at creation time, not at delivery time, so it does not
// defend against DNS rebinding -- a name that resolves publicly now can
// resolve to an internal address later. Closing that would mean resolving
// and pinning the address inside the delivery client itself, which is more
// than this admin-only surface warrants.
bool resolvesToBlockedAddress(const std::string& host);

} // namespace TicketHub::Common
