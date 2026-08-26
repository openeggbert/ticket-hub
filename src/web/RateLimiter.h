#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace TicketHub::Web {

// Fixed-window rate limiter (Phase 6, D124/D125): "simple fixed rate limit
// per IP/user ... no admin config, no per-endpoint/service-account
// exceptions." Deliberately in-memory only -- V1 has no job/cache
// infrastructure (docs/REMOVED_AND_DEFERRED_FEATURES.md), and a rate limit
// resetting on process restart is an acceptable, conservative tradeoff for a
// single-instance self-hosted install. Not shared across instances.
class RateLimiter {
public:
    RateLimiter(std::size_t maxRequestsPerWindow, std::chrono::steady_clock::duration window);

    // Returns true and records the attempt if `key` is still under its
    // fixed-window limit; returns false (without recording) once the limit
    // is reached until the window rolls over.
    bool allow(const std::string& key);

    // The same limit test as `allow`, but WITHOUT recording an attempt.
    // Split out for the login route (security audit 2026-08-26, finding H2):
    // that route must not consume budget for a *successful* sign-in, or a
    // legitimate user becomes the reason the limit trips -- which is exactly
    // what happened behind a reverse proxy, where every client shares the
    // proxy's IP and therefore one bucket.
    bool check(const std::string& key);

    // Records one attempt against `key`, whether or not it is still under
    // the limit. Pairs with `check` above.
    void record(const std::string& key);

private:
    struct Bucket {
        std::size_t count = 0;
        std::chrono::steady_clock::time_point windowStart{};
    };

    bool bucketExpiredLocked(const Bucket& bucket, std::chrono::steady_clock::time_point now) const;
    void maybeSweepLocked(std::chrono::steady_clock::time_point now);
    void sweepExpiredLocked(std::chrono::steady_clock::time_point now);

    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
    std::size_t maxRequestsPerWindow_;
    std::chrono::steady_clock::duration window_;
    std::size_t callsSinceSweep_ = 0;
};

} // namespace TicketHub::Web
