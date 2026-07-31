#include "common/PasswordHash.h"
#include "common/RandomToken.h"
#include "common/Sha256.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace TicketHub::Common;

    // Known-answer tests (NIST/common test vectors).
    require(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "sha256 of empty string");
    require(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "sha256 of \"abc\"");
    require(sha256Hex(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
           "sha256 across the padding block boundary (56 bytes)");
    require(sha256Hex(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
           "sha256 of exactly one block (64 bytes)");
    require(sha256Hex("a") != sha256Hex("b"), "sha256 detects a single-character difference");

    const auto token1 = randomTokenHex(32);
    const auto token2 = randomTokenHex(32);
    require(token1.size() == 64, "32-byte token hex-encodes to 64 characters");
    require(token1 != token2, "two generated tokens are not equal");

    const auto hash = hashPassword("correct horse battery staple");
    require(hash.rfind("$argon2id$", 0) == 0, "encoded hash uses the argon2id variant");
    require(verifyPassword(hash, "correct horse battery staple"), "correct password verifies");
    require(!verifyPassword(hash, "wrong password"), "incorrect password is rejected");
    require(hashPassword("correct horse battery staple") != hash, "identical passwords hash differently (random salt)");

    std::cout << "Crypto tests passed\n";
    return 0;
}
