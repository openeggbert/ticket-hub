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

// The caller is authenticated but lacks the fixed project role (or global
// administrator status) a write use case requires. Distinct from
// AuthenticationFailed (401: "who are you?") -- this is 403 ("I know who you
// are, and it's not enough").
class Forbidden final : public std::runtime_error {
public:
    explicit Forbidden(const std::string& message) : std::runtime_error(message) {}
};

// The caller supplied no Principal at all, and the installation's anonymous
// read-access toggle (D59, disabled by default) is off. Distinct from
// AuthenticationFailed -- this caller never attempted to log in, so there is
// no credential to have gotten wrong; it maps to 401 all the same.
class AuthenticationRequired final : public std::runtime_error {
public:
    explicit AuthenticationRequired(const std::string& message) : std::runtime_error(message) {}
};

} // namespace TicketHub::Domain
