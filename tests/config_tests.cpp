// Configuration defaults and start-up safety guards.
//
// Added by the 2026-08-26 security audit: finding C1 was that
// `AppConfig::seedDemo` defaulted to `true`, so every start path except
// `docker-compose.yml` silently created `demo@ticket-hub.local` -- a global
// administrator whose password is written in a comment in
// `migrations/*/002_seed_demo.sql` and whose Argon2id hash is committed to
// this repository. These tests pin the safe default and the guard that
// refuses the dangerous combination outright.

#include "config/Config.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void setEnv(const char* name, const char* value) {
    if (value == nullptr) {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
}

// Every environment variable these tests touch, cleared between cases so one
// case can never leak configuration into the next.
void clearEnvironment() {
    setEnv("TICKETHUB_SEED_DEMO", nullptr);
    setEnv("TICKETHUB_ALLOW_UNSAFE_DEMO_SEED", nullptr);
    setEnv("TICKETHUB_BIND_ADDRESS", nullptr);
}

bool seedingRejected(const TicketHub::Config::AppConfig& config) {
    try {
        config.requireSafeDemoSeeding();
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

} // namespace

int main() {
    using TicketHub::Config::AppConfig;

    // --- Defaults (C1) ---
    clearEnvironment();
    const AppConfig defaults = AppConfig::fromEnvironment();
    require(!defaults.seedDemo,
            "demo seeding is OFF by default -- it creates a global administrator with a published password");
    require(!defaults.allowUnsafeDemoSeed, "the unsafe-seed acknowledgement is OFF by default");
    require(defaults.bindAddress == "127.0.0.1", "the default bind address is loopback-only");

    // --- Loopback detection ---
    AppConfig config;
    for (const char* loopback : {"127.0.0.1", "127.0.1.1", "localhost", "::1"}) {
        config.bindAddress = loopback;
        require(config.bindsToLoopbackOnly(), std::string(loopback) + " is recognized as loopback-only");
    }
    // Fails closed: anything not explicitly recognized counts as publicly
    // reachable, so an unusual spelling errs toward refusing to seed.
    for (const char* public_ : {"0.0.0.0", "::", "192.168.1.10", "example.internal", ""}) {
        config.bindAddress = public_;
        require(!config.bindsToLoopbackOnly(),
                std::string("\"") + public_ + "\" is NOT treated as loopback-only");
    }

    // --- The guard itself ---
    config = AppConfig{};
    config.seedDemo = false;
    config.bindAddress = "0.0.0.0";
    require(!seedingRejected(config), "a public bind is fine when demo seeding is off");

    config.seedDemo = true;
    config.bindAddress = "127.0.0.1";
    require(!seedingRejected(config), "demo seeding is allowed on a loopback bind");

    config.bindAddress = "0.0.0.0";
    require(seedingRejected(config), "demo seeding on a public bind is refused (C1)");

    config.allowUnsafeDemoSeed = true;
    require(!seedingRejected(config),
            "the explicit TICKETHUB_ALLOW_UNSAFE_DEMO_SEED acknowledgement bypasses the guard");

    // --- The guard reads its inputs from the environment, end to end ---
    clearEnvironment();
    setEnv("TICKETHUB_SEED_DEMO", "true");
    setEnv("TICKETHUB_BIND_ADDRESS", "0.0.0.0");
    require(seedingRejected(AppConfig::fromEnvironment()),
            "TICKETHUB_SEED_DEMO=true with a public TICKETHUB_BIND_ADDRESS is refused");

    setEnv("TICKETHUB_ALLOW_UNSAFE_DEMO_SEED", "true");
    require(!seedingRejected(AppConfig::fromEnvironment()),
            "TICKETHUB_ALLOW_UNSAFE_DEMO_SEED=true from the environment bypasses the guard");
    clearEnvironment();

    std::cout << "config tests passed\n";
    return 0;
}
