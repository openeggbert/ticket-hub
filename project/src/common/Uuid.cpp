#include "common/Uuid.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace TicketHub::Common {

std::string uuidV4() {
    thread_local std::mt19937_64 engine(std::random_device{}());
    std::uniform_int_distribution<std::uint32_t> distribution(0, 255);
    std::array<std::uint8_t, 16> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(distribution(engine));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        output << std::setw(2) << static_cast<unsigned int>(bytes[index]);
        if (index == 3 || index == 5 || index == 7 || index == 9) {
            output << '-';
        }
    }
    return output.str();
}

std::string utcNowIso8601() {
    return utcNowPlusSecondsIso8601(0);
}

std::string utcNowPlusSecondsIso8601(const long long seconds) {
    const auto now = std::chrono::system_clock::now() + std::chrono::seconds(seconds);
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

} // namespace TicketHub::Common
