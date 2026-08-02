#include "web/RateLimiter.h"

namespace TicketHub::Web {

namespace {
constexpr std::size_t SweepIntervalCalls = 512;
}

RateLimiter::RateLimiter(std::size_t maxRequestsPerWindow, std::chrono::steady_clock::duration window)
    : maxRequestsPerWindow_(maxRequestsPerWindow), window_(window) {}

bool RateLimiter::allow(const std::string& key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);

    if (++callsSinceSweep_ >= SweepIntervalCalls) {
        callsSinceSweep_ = 0;
        sweepExpiredLocked(now);
    }

    auto& bucket = buckets_[key];
    if (bucket.count == 0 || now - bucket.windowStart >= window_) {
        bucket.count = 1;
        bucket.windowStart = now;
        return true;
    }
    if (bucket.count >= maxRequestsPerWindow_) {
        return false;
    }
    ++bucket.count;
    return true;
}

void RateLimiter::sweepExpiredLocked(std::chrono::steady_clock::time_point now) {
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        if (now - it->second.windowStart >= window_) {
            it = buckets_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace TicketHub::Web
