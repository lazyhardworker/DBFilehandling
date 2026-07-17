#include "DatabaseFile.h"
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace db {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static int sqliteCallback(void*, int, char**, char**) { return 0; }

std::string algorithmName(AlgorithmType type) {
    switch (type) {
        case AlgorithmType::LINEAR: return "Linear Scan";
        case AlgorithmType::HASH:   return "Hash Index";
        case AlgorithmType::BTREE:  return "B-Tree Index";
    }
    return "Unknown";
}

std::string tableName(AlgorithmType type) {
    switch (type) {
        case AlgorithmType::LINEAR: return "employees_linear";
        case AlgorithmType::HASH:   return "employees_hash";
        case AlgorithmType::BTREE:  return "employees_btree";
    }
    return "employees_linear";
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
DatabaseFile::DatabaseFile() : db_(nullptr) {}

DatabaseFile::~DatabaseFile() { close(); }

bool DatabaseFile::create(const std::string& path) {
    std::remove(path.c_str());
    return open(path);
}

bool DatabaseFile::open(const std::string& path) {
    close();
    path_ = path;
    int result = sqlite3_open_v2(
        path_.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        nullptr);
    if (result != SQLITE_OK) {
        std::cerr << "SQLite open failed: " << sqlite3_errmsg(db_) << "\n";
        close();
        return false;
    }
    // Performance pragmas: WAL mode + relaxed sync for benchmarking
    execute("PRAGMA journal_mode=WAL;");
    execute("PRAGMA synchronous=NORMAL;");
    execute("PRAGMA cache_size=-16000;"); // 16 MB page cache
    return initializeAllSchemas();
}

void DatabaseFile::close() {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    path_.clear();
}

bool DatabaseFile::isOpen() const { return db_ != nullptr; }

// ─────────────────────────────────────────────────────────────────────────────
// Transactions
// ─────────────────────────────────────────────────────────────────────────────
bool DatabaseFile::beginTransaction()    { return execute("BEGIN TRANSACTION;"); }
bool DatabaseFile::commitTransaction()   { return execute("COMMIT;"); }
bool DatabaseFile::rollbackTransaction() { return execute("ROLLBACK;"); }

// ─────────────────────────────────────────────────────────────────────────────
// CRUD
// ─────────────────────────────────────────────────────────────────────────────
uint64_t DatabaseFile::insertRecord(AlgorithmType type,
                                    const std::string& name,
                                    const std::string& email) {
    if (!isOpen()) return 0;
    const std::string sql =
        "INSERT INTO " + tableName(type) + " (name, email, deleted) VALUES (?, ?, 0);";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return 0;
    sqlite3_bind_text(stmt, 1, name.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return 0;
    return static_cast<uint64_t>(sqlite3_last_insert_rowid(db_));
}

std::optional<Record> DatabaseFile::readRecord(AlgorithmType type, uint64_t id) const {
    if (!isOpen()) return std::nullopt;
    const std::string sql =
        "SELECT id, deleted, name, email FROM " + tableName(type) + " WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return std::nullopt;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id));
    std::optional<Record> rec;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        rec = Record{
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
            sqlite3_column_int(stmt, 1) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
        };
    }
    sqlite3_finalize(stmt);
    return rec;
}

std::optional<Record> DatabaseFile::readRecordByName(AlgorithmType type,
                                                      const std::string& name) const {
    if (!isOpen()) return std::nullopt;
    // Linear: no index → full scan.  B-Tree: SQLite uses idx_btree_name.
    const std::string sql =
        "SELECT id, deleted, name, email FROM " + tableName(type)
        + " WHERE name = ? AND deleted = 0 LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return std::nullopt;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Record> rec;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        rec = Record{
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
            sqlite3_column_int(stmt, 1) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
        };
    }
    sqlite3_finalize(stmt);
    return rec;
}

bool DatabaseFile::updateRecord(AlgorithmType type, uint64_t id,
                                 const std::string& name,
                                 const std::string& email) {
    if (!isOpen()) return false;
    const std::string sql =
        "UPDATE " + tableName(type)
        + " SET name = ?, email = ? WHERE id = ? AND deleted = 0;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(id));
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool DatabaseFile::deleteRecord(AlgorithmType type, uint64_t id) {
    if (!isOpen()) return false;
    const std::string sql =
        "UPDATE " + tableName(type)
        + " SET deleted = 1 WHERE id = ? AND deleted = 0;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return false;
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id));
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

std::vector<Record> DatabaseFile::listRecords(AlgorithmType type, bool includeDeleted) const {
    std::vector<Record> records;
    if (!isOpen()) return records;
    const std::string sql = includeDeleted
        ? "SELECT id, deleted, name, email FROM " + tableName(type) + " ORDER BY id;"
        : "SELECT id, deleted, name, email FROM " + tableName(type)
          + " WHERE deleted = 0 ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return records;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        records.push_back(Record{
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
            sqlite3_column_int(stmt, 1) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
        });
    }
    sqlite3_finalize(stmt);
    return records;
}

// ── B-Tree advantage operations ───────────────────────────────────────────────
// For B-Tree: SQLite uses idx_btree_name → O(log n + k).
// For Linear/Hash: no index on name → full table scan O(n).

std::vector<Record> DatabaseFile::rangeQuery(AlgorithmType type,
                                              const std::string& low,
                                              const std::string& high) const {
    std::vector<Record> records;
    if (!isOpen()) return records;
    const std::string sql =
        "SELECT id, deleted, name, email FROM " + tableName(type)
        + " WHERE name >= ? AND name <= ? AND deleted = 0 ORDER BY name;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return records;
    sqlite3_bind_text(stmt, 1, low.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, high.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        records.push_back(Record{
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
            sqlite3_column_int(stmt, 1) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
        });
    }
    sqlite3_finalize(stmt);
    return records;
}

std::vector<Record> DatabaseFile::prefixSearch(AlgorithmType type,
                                                const std::string& prefix) const {
    // GLOB 'prefix*' is case-sensitive and index-friendly in SQLite.
    // SQLite will use idx_btree_name for B-Tree tables, full scan for others.
    std::vector<Record> records;
    if (!isOpen()) return records;
    const std::string pattern = prefix + "*";
    const std::string sql =
        "SELECT id, deleted, name, email FROM " + tableName(type)
        + " WHERE name GLOB ? AND deleted = 0 ORDER BY name;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return records;
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        records.push_back(Record{
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
            sqlite3_column_int(stmt, 1) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
        });
    }
    sqlite3_finalize(stmt);
    return records;
}

std::vector<Record> DatabaseFile::orderedScanByName(AlgorithmType type) const {
    // ORDER BY name: B-Tree traverses the sorted index directly (no filesort).
    // Linear/Hash must perform a full scan followed by an O(n log n) filesort.
    std::vector<Record> records;
    if (!isOpen()) return records;
    const std::string sql =
        "SELECT id, deleted, name, email FROM " + tableName(type)
        + " WHERE deleted = 0 ORDER BY name;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) return records;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        records.push_back(Record{
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
            sqlite3_column_int(stmt, 1) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))
        });
    }
    sqlite3_finalize(stmt);
    return records;
}


bool DatabaseFile::clearTable(AlgorithmType type) {
    return execute("DELETE FROM " + tableName(type) + ";");
}

bool DatabaseFile::compact() {
    if (!isOpen()) return false;
    return execute("VACUUM;");
}

uint64_t DatabaseFile::getFileSize() const {
    if (path_.empty()) return 0;
    try {
        return static_cast<uint64_t>(std::filesystem::file_size(path_));
    } catch (...) {
        return 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────
bool DatabaseFile::execute(const std::string& sql) const {
    if (!isOpen()) return false;
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), sqliteCallback, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQLite exec failed: "
                  << (errMsg ? errMsg : "unknown error") << "\n";
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool DatabaseFile::prepareStatement(const std::string& sql, sqlite3_stmt** stmt) const {
    if (!isOpen()) return false;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(),
                                 static_cast<int>(sql.size()), stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQLite prepare failed: " << sqlite3_errmsg(db_) << "\n";
        return false;
    }
    return true;
}

bool DatabaseFile::initializeAllSchemas() {
    // Linear — no extra index
    const std::string createLinear =
        "CREATE TABLE IF NOT EXISTS employees_linear ("
        "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name    TEXT    NOT NULL,"
        "  email   TEXT    NOT NULL,"
        "  deleted INTEGER NOT NULL DEFAULT 0"
        ");";

    // Hash — no SQLite index (index lives in application memory)
    const std::string createHash =
        "CREATE TABLE IF NOT EXISTS employees_hash ("
        "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name    TEXT    NOT NULL,"
        "  email   TEXT    NOT NULL,"
        "  deleted INTEGER NOT NULL DEFAULT 0"
        ");";

    // B-Tree — native SQLite index on 'name'
    const std::string createBtree =
        "CREATE TABLE IF NOT EXISTS employees_btree ("
        "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name    TEXT    NOT NULL,"
        "  email   TEXT    NOT NULL,"
        "  deleted INTEGER NOT NULL DEFAULT 0"
        ");";

    const std::string createBtreeIndex =
        "CREATE INDEX IF NOT EXISTS idx_btree_name ON employees_btree(name);";

    return execute(createLinear)
        && execute(createHash)
        && execute(createBtree)
        && execute(createBtreeIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────────────────────
std::string formatRecord(const Record& record) {
    return "Record { id=" + std::to_string(record.id)
        + ", deleted=" + (record.deleted ? "true" : "false")
        + ", name='"  + record.name
        + "', email='" + record.email + "' }";
}

} // namespace db
