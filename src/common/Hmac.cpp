#include "common/Hmac.h"

#include "common/Sha256.h"

#include <array>
#include <iomanip>
#include <sstream>

namespace TicketHub::Common {
namespace {
constexpr std::size_t BlockSize = 64; // SHA-256's block size, per RFC 2104/FIPS 198-1.
}

std::string hmacSha256Hex(const std::string& key, const std::string& message) {
    // RFC 2104: a key longer than the block size is itself hashed down to
    // the digest size first; a shorter one is zero-padded up to the block
    // size. Either way the result is exactly BlockSize bytes.
    std::array<unsigned char, BlockSize> blockKey{};
    if (key.size() > BlockSize) {
        const auto hashed = sha256Bytes(key);
        std::copy(hashed.begin(), hashed.end(), blockKey.begin());
    } else {
        std::copy(key.begin(), key.end(), blockKey.begin());
    }

    std::array<unsigned char, BlockSize> innerPad{};
    std::array<unsigned char, BlockSize> outerPad{};
    for (std::size_t i = 0; i < BlockSize; ++i) {
        innerPad[i] = static_cast<unsigned char>(blockKey[i] ^ 0x36U);
        outerPad[i] = static_cast<unsigned char>(blockKey[i] ^ 0x5cU);
    }

    std::string innerInput(innerPad.begin(), innerPad.end());
    innerInput += message;
    const auto innerDigest = sha256Bytes(innerInput);

    std::string outerInput(outerPad.begin(), outerPad.end());
    outerInput.append(reinterpret_cast<const char*>(innerDigest.data()), innerDigest.size());
    const auto outerDigest = sha256Bytes(outerInput);

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (const auto byte : outerDigest) {
        hex << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return hex.str();
}

} // namespace TicketHub::Common
