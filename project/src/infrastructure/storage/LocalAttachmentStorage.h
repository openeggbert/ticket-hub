#pragma once

#include <cstdint>
#include <string>

namespace TicketHub::Infrastructure::Storage {

// Local filesystem attachment storage (D15): hardwired, no abstract
// storage port/interface, and no extension point reserved for a future
// S3-compatible backend. A `storageKey` is an opaque handle (currently the
// attachment's own UUID) into a flat directory under `rootDirectory`; the
// caller never sees a real filesystem path.
class LocalAttachmentStorage {
public:
    explicit LocalAttachmentStorage(std::string rootDirectory);

    // Writes `bytes` under `storageKey`, creating the root directory if
    // needed. Overwrites silently if the key already exists (callers use a
    // fresh UUID per upload, so this should not happen in practice).
    void save(const std::string& storageKey, const std::string& bytes) const;
    std::string read(const std::string& storageKey) const;
    // A no-op if the file does not exist -- callers may race a periodic or
    // already-completed permanent delete.
    void remove(const std::string& storageKey) const;
    std::int64_t sizeOf(const std::string& storageKey) const;

private:
    std::string rootDirectory_;
    std::string pathFor(const std::string& storageKey) const;
};

} // namespace TicketHub::Infrastructure::Storage
