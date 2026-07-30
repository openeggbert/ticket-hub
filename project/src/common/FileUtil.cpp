#include "common/FileUtil.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace TicketHub::Common {

std::string readTextFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

} // namespace TicketHub::Common
