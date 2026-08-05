#include "infrastructure/storage/LocalAttachmentStorage.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace TicketHub::Infrastructure::Storage {

namespace {

// Defense in depth: `storageKey` is always a UUID we generate ourselves,
// never derived from user-supplied input, but this still guards against a
// path-traversal write/read if that assumption is ever violated by future
// code.
void requireSafeKey(const std::string& storageKey) {
    if (storageKey.empty()) {
        throw std::invalid_argument("Attachment storage key must not be empty");
    }
    for (const unsigned char ch : storageKey) {
        if (!std::isalnum(ch) && ch != '-') {
            throw std::invalid_argument("Attachment storage key contains an invalid character");
        }
    }
}

} // namespace

LocalAttachmentStorage::LocalAttachmentStorage(std::string rootDirectory) : rootDirectory_(std::move(rootDirectory)) {
    std::filesystem::create_directories(rootDirectory_);
}

std::string LocalAttachmentStorage::pathFor(const std::string& storageKey) const {
    requireSafeKey(storageKey);
    return (std::filesystem::path(rootDirectory_) / storageKey).string();
}

void LocalAttachmentStorage::save(const std::string& storageKey, const std::string& bytes) const {
    std::ofstream stream(pathFor(storageKey), std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Cannot write attachment file: " + storageKey);
    }
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string LocalAttachmentStorage::read(const std::string& storageKey) const {
    std::ifstream stream(pathFor(storageKey), std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot read attachment file: " + storageKey);
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void LocalAttachmentStorage::remove(const std::string& storageKey) const {
    std::error_code error;
    std::filesystem::remove(pathFor(storageKey), error);
}

std::int64_t LocalAttachmentStorage::sizeOf(const std::string& storageKey) const {
    std::error_code error;
    const auto size = std::filesystem::file_size(pathFor(storageKey), error);
    return error ? 0 : static_cast<std::int64_t>(size);
}

} // namespace TicketHub::Infrastructure::Storage
