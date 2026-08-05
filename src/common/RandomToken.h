#pragma once

#include <cstddef>
#include <string>

namespace TicketHub::Common {

// Cryptographically-suitable random token, hex-encoded. Used for session
// tokens (and, in a later phase, personal access tokens). The raw token is
// only ever returned once, at creation -- callers store its SHA-256 hash
// (see Sha256.h), never the token itself.
std::string randomTokenHex(std::size_t byteLength = 32);

} // namespace TicketHub::Common
