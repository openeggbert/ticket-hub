#include "web/RateLimiter.h"

#include <limits>

namespace TicketHub::Web {

namespace {
constexpr std::size_t SweepIntervalCalls = 512;
}

RateLimiter::RateLimiter(std::size_t maxRequestsPerWindow, std::chrono::steady_clock::duration window)
    : maxRequestsPerWindow_(maxRequestsPerWindow), window_(window) {}

bool RateLimiter::allow(const std::string& key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    maybeSweepLocked(now);

    auto& bucket = buckets_[key];
    if (bucketExpiredLocked(bucket, now)) {
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

bool RateLimiter::check(const std::string& key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);

    const auto found = buckets_.find(key);
    if (found == buckets_.end()) {
        return true;
    }
    if (bucketExpiredLocked(found->second, now)) {
        return true;
    }
    return found->second.count < maxRequestsPerWindow_;
}

void RateLimiter::record(const std::string& key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    maybeSweepLocked(now);

    auto& bucket = buckets_[key];
    if (bucketExpiredLocked(bucket, now)) {
        bucket.count = 1;
        bucket.windowStart = now;
        return;
    }
    // Saturate rather than wrap: a caller that keeps hammering a key past
    // the limit must not eventually roll `count` back under it.
    if (bucket.count < std::numeric_limits<std::size_t>::max()) {
        ++bucket.count;
    }
}

bool RateLimiter::bucketExpiredLocked(const Bucket& bucket,
                                      const std::chrono::steady_clock::time_point now) const {
    return bucket.count == 0 || now - bucket.windowStart >= window_;
}

void RateLimiter::maybeSweepLocked(const std::chrono::steady_clock::time_point now) {
    if (++callsSinceSweep_ >= SweepIntervalCalls) {
        callsSinceSweep_ = 0;
        sweepExpiredLocked(now);
    }
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
