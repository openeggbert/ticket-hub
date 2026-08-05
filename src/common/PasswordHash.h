#pragma once

#include <string>

namespace TicketHub::Common {

// Argon2id password hashing behind a small port. Encoded hashes are
// self-describing (algorithm/version/cost parameters/salt/hash all in one
// string), so verification never needs the original parameters passed back
// in. Never log the plaintext password or the raw hash bytes.
std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& encodedHash, const std::string& password);

} // namespace TicketHub::Common
