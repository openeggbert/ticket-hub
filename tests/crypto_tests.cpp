#include "common/Hmac.h"
#include "common/PasswordHash.h"
#include "common/RandomToken.h"
#include "common/Sha256.h"
#include "common/Uuid.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

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
    // --- UUID generation (security audit 2026-08-26, finding M3) ---
    // uuidV4 used to seed a thread_local Mersenne Twister from one
    // std::random_device draw. MT19937's state is reconstructible from its
    // output, and every id it produced is published through the API, so the
    // ids were predictable to anyone who collected enough of them. These
    // tests cannot prove randomness quality -- they pin the format and the
    // uniqueness property, including across threads, which is where a
    // per-thread seeded generator is most likely to collide.
    {
        const auto uuid = TicketHub::Common::uuidV4();
        require(uuid.size() == 36, "a UUID is 36 characters");
        require(uuid[8] == '-' && uuid[13] == '-' && uuid[18] == '-' && uuid[23] == '-',
                "a UUID has hyphens in the canonical positions");
        require(uuid[14] == '4', "a UUID declares version 4");
        require(uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b',
                "a UUID declares the RFC 4122 variant");
        for (std::size_t index = 0; index < uuid.size(); ++index) {
            if (index == 8 || index == 13 || index == 18 || index == 23) {
                continue;
            }
            const char character = uuid[index];
            require((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'),
                    "a UUID contains only lowercase hex digits and hyphens");
        }
    }

    {
        std::set<std::string> seen;
        for (int index = 0; index < 2000; ++index) {
            require(seen.insert(TicketHub::Common::uuidV4()).second, "uuidV4 does not repeat itself");
        }
    }

    {
        // Four threads, each generating ids concurrently. A generator seeded
        // per thread from a narrow seed space is exactly what would collide
        // here; drawing from std::random_device directly does not.
        constexpr int ThreadCount = 4;
        constexpr int PerThread = 500;
        std::vector<std::vector<std::string>> perThread(ThreadCount);
        std::vector<std::thread> threads;
        for (int index = 0; index < ThreadCount; ++index) {
            threads.emplace_back([&perThread, index] {
                for (int n = 0; n < PerThread; ++n) {
                    perThread[static_cast<std::size_t>(index)].push_back(TicketHub::Common::uuidV4());
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        std::set<std::string> seen;
        for (const auto& batch : perThread) {
            for (const auto& uuid : batch) {
                require(seen.insert(uuid).second, "uuidV4 does not collide across threads");
            }
        }
        require(seen.size() == ThreadCount * PerThread, "every generated UUID is distinct");
    }

    // Argon2id salts are drawn the same way now; two hashes of the same
    // password must therefore differ.
    require(hashPassword("the-same-password") != hashPassword("the-same-password"),
            "each password hash uses a fresh salt");

    return 0;
}
