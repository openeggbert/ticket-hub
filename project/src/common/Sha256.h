#pragma once

#include <string>

namespace TicketHub::Common {

// Fast, non-keyed digest used only to store an irreversible lookup fingerprint
// for high-entropy random tokens (session tokens, PATs). Never use this for
// passwords -- those go through the slow, salted Argon2id path in
// PasswordHash.h instead.
std::string sha256Hex(const std::string& input);

} // namespace TicketHub::Common
