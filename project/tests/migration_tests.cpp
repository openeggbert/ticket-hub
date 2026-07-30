#include "infrastructure/database/Migration.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using TicketHub::Infrastructure::Database::discoverMigrationFiles;
    using TicketHub::Infrastructure::Database::migrationChecksum;

    const fs::path directory = fs::temp_directory_path() / "ticket-hub-migration-discovery";
    std::error_code error;
    fs::remove_all(directory, error);
    fs::create_directories(directory);

    std::ofstream(directory / "003_third.sql") << "SELECT 3;\n";
    std::ofstream(directory / "001_first.sql") << "SELECT 1;\n";
    std::ofstream(directory / "002_seed_demo.sql") << "SELECT 2;\n";
    std::ofstream(directory / "README.txt") << "ignored\n";

    const auto migrations = discoverMigrationFiles(directory);
    require(migrations.size() == 2, "seed and unrelated files are excluded");
    require(migrations[0].version == "001_first", "migrations are ordered");
    require(migrations[1].version == "003_third", "later migration is discovered");
    require(migrations[0].checksum == migrationChecksum("SELECT 1;\n"), "checksum is stable");
    require(migrationChecksum("a") != migrationChecksum("b"), "checksum detects content changes");

    fs::remove_all(directory, error);
    std::cout << "Migration tests passed\n";
    return 0;
}
