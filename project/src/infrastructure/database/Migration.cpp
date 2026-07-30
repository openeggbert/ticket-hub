#include "infrastructure/database/Migration.h"

#include "common/FileUtil.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace TicketHub::Infrastructure::Database {

std::string migrationChecksum(const std::string_view content) {
    // Stable FNV-1a checksum. This detects accidental modification of applied
    // migrations without adding a mandatory crypto-library dependency.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : content) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::vector<MigrationFile> discoverMigrationFiles(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) {
        throw std::runtime_error("Migration directory does not exist: " + directory.string());
    }
    if (!std::filesystem::is_directory(directory)) {
        throw std::runtime_error("Migration path is not a directory: " + directory.string());
    }

    static const std::regex pattern(R"(^([0-9]{3,})_[A-Za-z0-9_-]+\.sql$)");
    std::vector<MigrationFile> migrations;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (filename.find("_seed_") != std::string::npos) {
            continue;
        }
        std::smatch match;
        if (!std::regex_match(filename, match, pattern)) {
            continue;
        }
        const std::string version = entry.path().stem().string();
        const std::string content = Common::readTextFile(entry.path().string());
        migrations.push_back(MigrationFile{version, entry.path(), migrationChecksum(content)});
    }

    std::sort(migrations.begin(), migrations.end(), [](const MigrationFile& left, const MigrationFile& right) {
        return left.version < right.version;
    });

    if (migrations.empty()) {
        throw std::runtime_error("No schema migrations found in: " + directory.string());
    }
    return migrations;
}

} // namespace TicketHub::Infrastructure::Database
