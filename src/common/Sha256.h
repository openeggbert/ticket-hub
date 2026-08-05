#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace TicketHub::Common {

// Fast, non-keyed digest used only to store an irreversible lookup fingerprint
// for high-entropy random tokens (session tokens, PATs). Never use this for
// passwords -- those go through the slow, salted Argon2id path in
// PasswordHash.h instead.
std::string sha256Hex(const std::string& input);

// The raw 32-byte digest, for callers that need to feed it into a further
// construction (e.g. Common::hmacSha256, common/Hmac.h) rather than display
// it -- sha256Hex is exactly this, hex-encoded.
std::array<unsigned char, 32> sha256Bytes(const std::string& input);

} // namespace TicketHub::Common
