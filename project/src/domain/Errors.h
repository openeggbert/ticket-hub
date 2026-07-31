#pragma once

#include <stdexcept>
#include <string>

namespace TicketHub::Domain {

class ConcurrencyConflict final : public std::runtime_error {
public:
    explicit ConcurrencyConflict(const std::string& message) : std::runtime_error(message) {}
};

// Deliberately generic: email-not-found and wrong-password must be
// indistinguishable to the caller, or the login endpoint becomes a user-
// enumeration oracle.
class AuthenticationFailed final : public std::runtime_error {
public:
    explicit AuthenticationFailed(const std::string& message) : std::runtime_error(message) {}
};

// Distinct from AuthenticationFailed so the web layer can, if it chooses,
// surface a slightly more specific (but still non-enumerating) message.
class AccountLocked final : public std::runtime_error {
public:
    explicit AccountLocked(const std::string& message) : std::runtime_error(message) {}
};

} // namespace TicketHub::Domain
