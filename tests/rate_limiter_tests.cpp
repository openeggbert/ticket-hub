#include "web/RateLimiter.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using TicketHub::Web::RateLimiter;

    {
        RateLimiter limiter(3, std::chrono::seconds(60));
        require(limiter.allow("1.2.3.4"), "first request in window is allowed");
        require(limiter.allow("1.2.3.4"), "second request in window is allowed");
        require(limiter.allow("1.2.3.4"), "third request in window is allowed");
        require(!limiter.allow("1.2.3.4"), "fourth request in window is rejected");
        require(!limiter.allow("1.2.3.4"), "limit stays tripped within the same window");
    }

    {
        RateLimiter limiter(1, std::chrono::seconds(60));
        require(limiter.allow("user-a"), "key A gets its own bucket");
        require(limiter.allow("user-b"), "key B is independent of key A's bucket");
        require(!limiter.allow("user-a"), "key A is now over its own limit");
        require(!limiter.allow("user-b"), "key B is now over its own limit");
    }

    {
        RateLimiter limiter(1, std::chrono::milliseconds(50));
        require(limiter.allow("resets"), "first request allowed");
        require(!limiter.allow("resets"), "second immediate request rejected");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        require(limiter.allow("resets"), "request allowed again once the window rolls over");
    }

    // --- check()/record() split (security audit 2026-08-26, finding H2) ---
    // The login route must be able to test the limit without spending it, so
    // a *successful* sign-in never pushes the bucket toward the limit. Before
    // this split the route called allow() on every request, which is why a
    // correct credential could return 429 once an attacker had filled the
    // shared per-IP bucket.
    {
        RateLimiter limiter(2, std::chrono::seconds(60));
        require(limiter.check("k"), "check() passes on an untouched key");
        require(limiter.check("k"), "check() is side-effect free -- repeating it does not consume budget");
        require(limiter.check("k"), "check() still passes after many checks");

        limiter.record("k");
        require(limiter.check("k"), "one recorded attempt is still under a limit of 2");
        limiter.record("k");
        require(!limiter.check("k"), "two recorded attempts reach the limit of 2");

        require(limiter.check("other"), "a different key is unaffected");
    }

    {
        // record() must keep counting past the limit rather than wrapping,
        // so sustained abuse cannot roll the counter back under the limit.
        RateLimiter limiter(1, std::chrono::seconds(60));
        for (int i = 0; i < 50; ++i) {
            limiter.record("flood");
        }
        require(!limiter.check("flood"), "the key stays over its limit after repeated record() calls");
    }

    {
        RateLimiter limiter(1, std::chrono::milliseconds(50));
        limiter.record("rolls");
        require(!limiter.check("rolls"), "recorded attempt trips the limit");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        require(limiter.check("rolls"), "check() sees the window roll over");
        limiter.record("rolls");
        require(!limiter.check("rolls"), "the new window counts from the first attempt in it");
    }

    {
        // allow() and the check()/record() pair share one bucket per key.
        RateLimiter limiter(2, std::chrono::seconds(60));
        require(limiter.allow("shared"), "allow() records an attempt");
        limiter.record("shared");
        require(!limiter.check("shared"), "check() sees what allow() recorded");
        require(!limiter.allow("shared"), "allow() sees what record() recorded");
    }

    std::cout << "All rate limiter tests passed.\n";
    return 0;
}
