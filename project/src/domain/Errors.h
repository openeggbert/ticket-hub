#pragma once

#include <stdexcept>
#include <string>

namespace TicketHub::Domain {

class ConcurrencyConflict final : public std::runtime_error {
public:
    explicit ConcurrencyConflict(const std::string& message) : std::runtime_error(message) {}
};

} // namespace TicketHub::Domain
