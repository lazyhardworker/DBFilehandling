#include "DatabaseFile.h"
#include <cstdio>
#include <iostream>

using namespace std;

namespace db {

static int sqliteCallback(void*, int, char**, char**) {
    return 0;
}

DatabaseFile::DatabaseFile()
    : db_(nullptr) {
}

DatabaseFile::~DatabaseFile() {
    close();
}

bool DatabaseFile::create(const string& path) {
    remove(path.c_str());
    return open(path);
}

bool DatabaseFile::open(const string& path) {
    close();
    path_ = path;
    int result = sqlite3_open_v2(path_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite open failed: " << sqlite3_errmsg(db_) << "\n";
        close();
        return false;
    }
    return initializeSchema();
}

void DatabaseFile::close() {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    path_.clear();
}

bool DatabaseFile::isOpen() const {
    return db_ != nullptr;
}

uint64_t DatabaseFile::insertRecord(const string& name, const string& email) {
    if (!isOpen()) {
        return 0;
    }
    const string sql = "INSERT INTO employees (name, email, deleted) VALUES (?, ?, 0);";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (result != SQLITE_DONE) {
        return 0;
    }
    return static_cast<uint64_t>(sqlite3_last_insert_rowid(db_));
}

optional<Record> DatabaseFile::readRecord(uint64_t id) const {
    if (!isOpen()) {
        return nullopt;
    }
    const string sql = "SELECT id, deleted, name, email FROM employees WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) {
        return nullopt;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id));
    optional<Record> record;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        record = Record{
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
            sqlite3_column_int(stmt, 1) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))};
    }
    sqlite3_finalize(stmt);
    return record;
}

bool DatabaseFile::updateRecord(uint64_t id, const string& name, const string& email) {
    if (!isOpen()) {
        return false;
    }
    const string sql = "UPDATE employees SET name = ?, email = ? WHERE id = ? AND deleted = 0;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(id));
    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return result == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool DatabaseFile::deleteRecord(uint64_t id) {
    if (!isOpen()) {
        return false;
    }
    const string sql = "UPDATE employees SET deleted = 1 WHERE id = ? AND deleted = 0;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(id));
    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return result == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

vector<Record> DatabaseFile::listRecords(bool includeDeleted) const {
    vector<Record> records;
    if (!isOpen()) {
        return records;
    }
    const string sql = includeDeleted
        ? "SELECT id, deleted, name, email FROM employees ORDER BY id;"
        : "SELECT id, deleted, name, email FROM employees WHERE deleted = 0 ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, &stmt)) {
        return records;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        records.push_back(Record{
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
            sqlite3_column_int(stmt, 1) != 0,
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))});
    }
    sqlite3_finalize(stmt);
    return records;
}

bool DatabaseFile::compact() {
    if (!isOpen()) {
        return false;
    }
    return execute("VACUUM;");
}

bool DatabaseFile::execute(const string& sql) const {
    if (!isOpen()) {
        return false;
    }
    char* errMsg = nullptr;
    int result = sqlite3_exec(db_, sql.c_str(), sqliteCallback, nullptr, &errMsg);
    if (result != SQLITE_OK) {
        cerr << "SQLite execute failed: " << (errMsg ? errMsg : "unknown error") << "\n";
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool DatabaseFile::prepareStatement(const string& sql, sqlite3_stmt** stmt) const {
    if (!isOpen()) {
        return false;
    }
    int result = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare failed: " << sqlite3_errmsg(db_) << "\n";
        return false;
    }
    return true;
}

bool DatabaseFile::initializeSchema() {
    const string sql =
        "CREATE TABLE IF NOT EXISTS employees ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "email TEXT NOT NULL,"
        "deleted INTEGER NOT NULL DEFAULT 0"
        ");";
    return execute(sql);
}

string formatRecord(const Record& record) {
    return "Record { id=" + to_string(record.id)
        + ", deleted=" + (record.deleted ? "true" : "false")
        + ", name='" + record.name + "', email='" + record.email + "' }";
}

} // namespace db
