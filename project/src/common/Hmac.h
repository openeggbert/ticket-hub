#pragma once

#include <string>

namespace TicketHub::Common {

// HMAC-SHA256, hex-encoded. Used to sign outbound webhook payloads (D39/D41)
// so a receiver can verify a delivery actually came from this installation
// and was not tampered with in transit -- the same purpose GitHub/Stripe use
// their own X-Hub-Signature-style headers for. Built on Common::sha256Bytes
// (this project's own hand-rolled SHA-256, common/Sha256.h) rather than
// adding an OpenSSL/libcrypto dependency just for this one construction.
std::string hmacSha256Hex(const std::string& key, const std::string& message);

} // namespace TicketHub::Common
