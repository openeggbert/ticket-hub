#include "common/Hmac.h"
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

    // HMAC-SHA256 (D39/D41 webhook signing): RFC 4231 test case 1 --
    // key = 0x0b repeated 20 times, data = "Hi There".
    require(hmacSha256Hex(std::string(20, static_cast<char>(0x0b)), "Hi There") ==
                "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
           "hmacSha256Hex matches RFC 4231 test case 1");
    // RFC 4231 test case 2 -- key = "Jefe", data = "what do ya want for nothing?".
    require(hmacSha256Hex("Jefe", "what do ya want for nothing?") ==
                "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
           "hmacSha256Hex matches RFC 4231 test case 2");
    // A key longer than SHA-256's 64-byte block size must itself be hashed
    // down first (RFC 2104) -- RFC 4231 test case 6, a 131-byte key.
    require(hmacSha256Hex(std::string(131, static_cast<char>(0xaa)), "Test Using Larger Than Block-Size Key - Hash Key First") ==
                "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
           "hmacSha256Hex hashes an over-block-size key down first (RFC 4231 test case 6)");
    require(hmacSha256Hex("key1", "message") != hmacSha256Hex("key2", "message"),
           "hmacSha256Hex is sensitive to the key, not just the message");

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
