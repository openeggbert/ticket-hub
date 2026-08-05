#include "common/PasswordHash.h"

#include <argon2.h>

#include <array>
#include <random>
#include <stdexcept>
#include <vector>

namespace TicketHub::Common {
namespace {

// OWASP-recommended minimums for Argon2id as of the 2026 cheat sheet:
// m=19 MiB, t=2, p=1. Keep these named so a future hardening pass can tune
// them without hunting for magic numbers.
constexpr std::uint32_t TimeCost = 2;
constexpr std::uint32_t MemoryCostKiB = 19456;
constexpr std::uint32_t Parallelism = 1;
constexpr std::size_t SaltLength = 16;
constexpr std::size_t HashLength = 32;

std::vector<std::uint8_t> randomSalt() {
    thread_local std::mt19937_64 engine(std::random_device{}());
    std::uniform_int_distribution<int> distribution(0, 255);
    std::vector<std::uint8_t> salt(SaltLength);
    for (auto& byte : salt) {
        byte = static_cast<std::uint8_t>(distribution(engine));
    }
    return salt;
}

} // namespace

std::string hashPassword(const std::string& password) {
    const auto salt = randomSalt();
    const std::size_t encodedLength = argon2_encodedlen(
        TimeCost, MemoryCostKiB, Parallelism, static_cast<std::uint32_t>(salt.size()),
        static_cast<std::uint32_t>(HashLength), Argon2_id);
    std::string encoded(encodedLength, '\0');

    const int result = argon2id_hash_encoded(
        TimeCost, MemoryCostKiB, Parallelism, password.data(), password.size(), salt.data(), salt.size(),
        HashLength, encoded.data(), encoded.size());
    if (result != ARGON2_OK) {
        throw std::runtime_error(std::string("Argon2id hashing failed: ") + argon2_error_message(result));
    }
    // argon2_encodedlen includes the trailing NUL; trim it so std::string::size() is exact.
    const auto nul = encoded.find('\0');
    if (nul != std::string::npos) {
        encoded.resize(nul);
    }
    return encoded;
}

bool verifyPassword(const std::string& encodedHash, const std::string& password) {
    const int result = argon2id_verify(encodedHash.c_str(), password.data(), password.size());
    if (result == ARGON2_OK) {
        return true;
    }
    if (result == ARGON2_VERIFY_MISMATCH) {
        return false;
    }
    // A malformed stored hash is a data problem, not a "wrong password";
    // callers should treat this the same as verification failure rather than
    // crash a login attempt, but it is worth distinguishing in logs.
    return false;
}

} // namespace TicketHub::Common
