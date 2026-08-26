// Outbound-target address checks (security audit 2026-08-26, finding M5).
//
// Only the pure, offline parts are tested here: URL host extraction and the
// IP-range classification. `resolvesToBlockedAddress` is exercised only for
// IP literals and for a name that cannot resolve, so the suite never depends
// on DNS being reachable from the build machine.

#include "common/NetworkAddress.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void requireHost(const std::string& url, const std::string& expected) {
    const auto host = TicketHub::Common::hostFromHttpUrl(url);
    require(host.has_value() && *host == expected,
            "host of \"" + url + "\" is \"" + expected + "\" (got \"" + host.value_or("<none>") + "\")");
}

void requireNoHost(const std::string& url) {
    require(!TicketHub::Common::hostFromHttpUrl(url).has_value(), "\"" + url + "\" yields no host");
}

void requireBlocked(const std::string& ip) {
    require(TicketHub::Common::isBlockedIpLiteral(ip), ip + " must be refused as an outbound target");
}

void requireAllowed(const std::string& ip) {
    require(!TicketHub::Common::isBlockedIpLiteral(ip), ip + " is a public address and should be allowed");
}

} // namespace

int main() {
    using namespace TicketHub::Common;

    // --- Host extraction ---
    requireHost("http://example.com/hook", "example.com");
    requireHost("https://example.com", "example.com");
    requireHost("https://example.com:8443/path?x=1#frag", "example.com");
    requireHost("HTTPS://Example.COM/hook", "Example.COM");
    requireHost("http://127.0.0.1:8080/", "127.0.0.1");
    // Userinfo must be discarded, or `http://example.com@127.0.0.1/` reads as
    // a request to example.com while actually targeting loopback.
    requireHost("http://user@127.0.0.1/", "127.0.0.1");
    requireHost("http://user:p@ss@127.0.0.1/", "127.0.0.1");
    requireHost("http://[::1]:9000/hook", "::1");
    requireHost("http://[fe80::1]/", "fe80::1");
    requireNoHost("ftp://example.com/");
    requireNoHost("file:///etc/passwd");
    requireNoHost("gopher://example.com/");
    requireNoHost("http://");
    requireNoHost("http:///path");
    requireNoHost("");
    requireNoHost("http://[]/");

    // --- IPv4 ranges ---
    requireBlocked("127.0.0.1");
    requireBlocked("127.1.2.3");
    requireBlocked("0.0.0.0");
    requireBlocked("10.0.0.1");
    requireBlocked("172.16.0.1");
    requireBlocked("172.31.255.254");
    requireBlocked("192.168.1.1");
    requireBlocked("169.254.1.1");
    requireBlocked("169.254.169.254"); // the cloud metadata address
    requireBlocked("100.64.0.1");      // carrier-grade NAT
    requireBlocked("198.18.0.1");      // benchmarking
    requireBlocked("224.0.0.1");       // multicast
    requireBlocked("255.255.255.255");
    requireBlocked("192.0.0.1");

    // Neighbouring addresses that are genuinely public must still work --
    // an over-broad block would quietly break real webhook targets.
    requireAllowed("8.8.8.8");
    requireAllowed("1.1.1.1");
    requireAllowed("172.15.0.1");
    requireAllowed("172.32.0.1");
    requireAllowed("192.167.1.1");
    requireAllowed("192.169.1.1");
    requireAllowed("100.63.255.255");
    requireAllowed("100.128.0.1");
    requireAllowed("198.17.255.255");
    requireAllowed("198.20.0.1");
    requireAllowed("223.255.255.255");

    // --- IPv6 ranges ---
    requireBlocked("::1");
    requireBlocked("::");
    requireBlocked("fc00::1");        // unique-local
    requireBlocked("fd12:3456::1");   // unique-local
    requireBlocked("fe80::1");        // link-local
    requireBlocked("ff02::1");        // multicast
    // IPv4-mapped forms must be judged on the embedded address.
    requireBlocked("::ffff:127.0.0.1");
    requireBlocked("::ffff:169.254.169.254");
    requireBlocked("::ffff:10.0.0.1");
    requireAllowed("2001:4860:4860::8888");
    requireAllowed("::ffff:8.8.8.8");

    // --- Not an IP literal at all: the caller resolves those ---
    require(!isBlockedIpLiteral("example.com"), "a host name is not classified as a blocked literal");
    require(!isBlockedIpLiteral(""), "an empty string is not classified as a blocked literal");
    require(!isBlockedIpLiteral("999.999.999.999"), "an invalid dotted quad is not an IP literal");

    // --- resolvesToBlockedAddress, offline cases only ---
    require(resolvesToBlockedAddress("127.0.0.1"), "an IP literal is judged without resolving");
    require(resolvesToBlockedAddress("::1"), "an IPv6 literal is judged without resolving");
    require(resolvesToBlockedAddress(""), "an empty host is refused");
    // Fails closed: `.invalid` is reserved by RFC 2606 and never resolves.
    require(resolvesToBlockedAddress("ticket-hub-should-never-resolve.invalid"),
            "a host that cannot be resolved is refused rather than allowed through");

    std::cout << "network address tests passed\n";
    return 0;
}
