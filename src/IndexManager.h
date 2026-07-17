#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace db {

/// In-memory hash index mapping a string key (name) to a SQLite rowid.
/// Used by the HASH algorithm strategy to achieve O(1) average-case lookups.
class IndexManager {
public:
    IndexManager() = default;

    /// Insert or overwrite a key → rowid mapping.
    void insert(const std::string& key, uint64_t rowid);

    /// Look up a rowid by key.  Returns nullopt if not found.
    std::optional<uint64_t> find(const std::string& key) const;

    /// Remove a key from the index.
    void remove(const std::string& key);

    /// Clear all entries.
    void clear();

    /// Number of entries currently in the index.
    std::size_t size() const;

private:
    std::unordered_map<std::string, uint64_t> index_;
};

} // namespace db
