#include "common/NetworkAddress.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace TicketHub::Common {
namespace {

using V4 = std::array<std::uint8_t, 4>;
using V6 = std::array<std::uint8_t, 16>;

bool blockedV4(const V4& octets) {
    const std::uint8_t a = octets[0];
    const std::uint8_t b = octets[1];
    if (a == 0) return true;                                   // 0.0.0.0/8, "this network"
    if (a == 10) return true;                                  // RFC 1918
    if (a == 100 && b >= 64 && b <= 127) return true;          // 100.64.0.0/10, carrier-grade NAT
    if (a == 127) return true;                                 // loopback
    if (a == 169 && b == 254) return true;                     // link-local, incl. 169.254.169.254 metadata
    if (a == 172 && b >= 16 && b <= 31) return true;           // RFC 1918
    if (a == 192 && b == 0 && octets[2] == 0) return true;     // IETF protocol assignments
    if (a == 192 && b == 168) return true;                     // RFC 1918
    if (a == 198 && (b == 18 || b == 19)) return true;         // benchmarking
    if (a >= 224) return true;                                 // multicast, reserved, broadcast
    return false;
}

bool blockedV6(const V6& bytes) {
    const bool allZeroPrefix = std::all_of(bytes.begin(), bytes.begin() + 15,
                                           [](const std::uint8_t byte) { return byte == 0; });
    if (allZeroPrefix && (bytes[15] == 0 || bytes[15] == 1)) {
        return true; // :: (unspecified) and ::1 (loopback)
    }
    // IPv4-mapped (::ffff:a.b.c.d) and IPv4-compatible: judge the embedded
    // IPv4 address, or `http://[::ffff:127.0.0.1]/` would sail past.
    const bool v4Mapped = std::all_of(bytes.begin(), bytes.begin() + 10,
                                      [](const std::uint8_t byte) { return byte == 0; }) &&
                          bytes[10] == 0xFF && bytes[11] == 0xFF;
    if (v4Mapped) {
        return blockedV4(V4{bytes[12], bytes[13], bytes[14], bytes[15]});
    }
    if ((bytes[0] & 0xFE) == 0xFC) return true;                       // fc00::/7 unique-local
    if (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80) return true;   // fe80::/10 link-local
    if (bytes[0] == 0xFF) return true;                                // ff00::/8 multicast
    return false;
}

} // namespace

std::optional<std::string> hostFromHttpUrl(const std::string& url) {
    std::string lowered = url;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });

    std::size_t start = 0;
    if (lowered.rfind("http://", 0) == 0) {
        start = 7;
    } else if (lowered.rfind("https://", 0) == 0) {
        start = 8;
    } else {
        return std::nullopt;
    }

    // Authority runs to the first '/', '?' or '#'.
    const auto end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);

    // Drop any userinfo: everything up to the LAST '@' (a password may
    // legitimately contain '@').
    const auto at = authority.rfind('@');
    if (at != std::string::npos) {
        authority = authority.substr(at + 1);
    }
    if (authority.empty()) {
        return std::nullopt;
    }

    // A bracketed IPv6 literal keeps its colons; anything else is split at
    // the first colon to drop the port.
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos || close == 1) {
            return std::nullopt;
        }
        return authority.substr(1, close - 1);
    }
    const auto colon = authority.find(':');
    std::string host = colon == std::string::npos ? authority : authority.substr(0, colon);
    if (host.empty()) {
        return std::nullopt;
    }
    return host;
}

bool isBlockedIpLiteral(const std::string& text) {
    V4 v4{};
    if (inet_pton(AF_INET, text.c_str(), v4.data()) == 1) {
        return blockedV4(v4);
    }
    V6 v6{};
    if (inet_pton(AF_INET6, text.c_str(), v6.data()) == 1) {
        return blockedV6(v6);
    }
    return false;
}

bool resolvesToBlockedAddress(const std::string& host) {
    if (host.empty()) {
        return true;
    }
    if (isBlockedIpLiteral(host)) {
        return true;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &results) != 0 || results == nullptr) {
        if (results != nullptr) {
            freeaddrinfo(results);
        }
        return true;
    }

    bool blocked = false;
    for (const addrinfo* entry = results; entry != nullptr && !blocked; entry = entry->ai_next) {
        if (entry->ai_family == AF_INET) {
            const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
            V4 octets{};
            std::memcpy(octets.data(), &address->sin_addr, octets.size());
            blocked = blockedV4(octets);
        } else if (entry->ai_family == AF_INET6) {
            const auto* address = reinterpret_cast<const sockaddr_in6*>(entry->ai_addr);
            V6 bytes{};
            std::memcpy(bytes.data(), &address->sin6_addr, bytes.size());
            blocked = blockedV6(bytes);
        }
    }
    freeaddrinfo(results);
    return blocked;
}

} // namespace TicketHub::Common
