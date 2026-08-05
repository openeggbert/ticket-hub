#include "common/RandomToken.h"

#include <iomanip>
#include <random>
#include <sstream>

namespace TicketHub::Common {

std::string randomTokenHex(const std::size_t byteLength) {
    // Pull directly from std::random_device for every byte rather than
    // seeding a PRNG once (as Uuid.cpp does for UUIDs, where the lower
    // entropy budget is an accepted tradeoff) -- session tokens are
    // security-sensitive and generated rarely enough that the extra
    // random_device calls cost nothing.
    std::random_device source;
    std::uniform_int_distribution<int> distribution(0, 255);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < byteLength; ++index) {
        output << std::setw(2) << distribution(source);
    }
    return output.str();
}

} // namespace TicketHub::Common
