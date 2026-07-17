#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace db {

// ─────────────────────────────────────────────────────────────────────────────
// Algorithm types
// ─────────────────────────────────────────────────────────────────────────────
enum class AlgorithmType {
    LINEAR = 0,   ///< No index  — full sequential table scan
    HASH   = 1,   ///< Application-level in-memory unordered_map index
    BTREE  = 2    ///< Native SQLite CREATE INDEX (B-Tree, O(log n))
};

/// Human-readable algorithm name.
std::string algorithmName(AlgorithmType type);

/// SQLite table name for each algorithm.
std::string tableName(AlgorithmType type);

// ─────────────────────────────────────────────────────────────────────────────
// Data model
// ─────────────────────────────────────────────────────────────────────────────
struct Record {
    uint64_t    id;
    bool        deleted;
    std::string name;
    std::string email;
};

// ─────────────────────────────────────────────────────────────────────────────
// DatabaseFile
// ─────────────────────────────────────────────────────────────────────────────
class DatabaseFile {
public:
    DatabaseFile();
    ~DatabaseFile();

    // Lifecycle
    bool create(const std::string& path);   ///< Delete then create fresh.
    bool open(const std::string& path);
    void close();
    bool isOpen() const;

    // Transaction control
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // Algorithm-dispatching CRUD ─────────────────────────────────────────────
    uint64_t              insertRecord(AlgorithmType type, const std::string& name, const std::string& email);
    std::optional<Record> readRecord(AlgorithmType type, uint64_t id) const;
    std::optional<Record> readRecordByName(AlgorithmType type, const std::string& name) const;
    bool                  updateRecord(AlgorithmType type, uint64_t id, const std::string& name, const std::string& email);
    bool                  deleteRecord(AlgorithmType type, uint64_t id);
    std::vector<Record>   listRecords(AlgorithmType type, bool includeDeleted = false) const;

    // B-Tree-advantage operations ─────────────────────────────────────────────
    /// Range query: name >= low AND name <= high (uses B-Tree index, O(log n + k)).
    /// Hash/Linear must do a full table scan, O(n).
    std::vector<Record>   rangeQuery(AlgorithmType type, const std::string& low, const std::string& high) const;

    /// Prefix search via GLOB 'prefix*' (B-Tree index-friendly, O(log n + k)).
    /// Hash/Linear must do a full table scan, O(n).
    std::vector<Record>   prefixSearch(AlgorithmType type, const std::string& prefix) const;

    /// Scan all non-deleted records ordered by name.
    /// B-Tree traverses the index in order (no extra sort).  Others need filesort O(n log n).
    std::vector<Record>   orderedScanByName(AlgorithmType type) const;

    bool                  clearTable(AlgorithmType type);

    // Maintenance
    bool     compact();
    uint64_t getFileSize() const;
    const std::string& getPath() const { return path_; }

private:
    bool execute(const std::string& sql) const;
    bool prepareStatement(const std::string& sql, sqlite3_stmt** stmt) const;
    bool initializeAllSchemas();

    sqlite3*    db_;
    std::string path_;
};

// Utility
std::string formatRecord(const Record& record);

} // namespace db
