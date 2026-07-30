#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace TicketHub::Infrastructure::Database {

struct MigrationFile {
    std::string version;
    std::filesystem::path path;
    std::string checksum;
};

std::string migrationChecksum(std::string_view content);
std::vector<MigrationFile> discoverMigrationFiles(const std::filesystem::path& directory);

} // namespace TicketHub::Infrastructure::Database
