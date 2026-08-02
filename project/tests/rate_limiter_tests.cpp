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

    std::cout << "All rate limiter tests passed.\n";
    return 0;
}
