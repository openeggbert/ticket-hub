#pragma once

#include <string>

namespace TicketHub::Common {

std::string uuidV4();
std::string utcNowIso8601();
std::string utcNowPlusSecondsIso8601(long long seconds);

} // namespace TicketHub::Common
