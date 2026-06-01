#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace db {

struct Record {
    uint64_t id;
    bool deleted;
    std::string name;
    std::string email;
};

class DatabaseFile {
public:
    DatabaseFile();
    ~DatabaseFile();

    bool create(const std::string& path);
    bool open(const std::string& path);
    void close();
    bool isOpen() const;

    uint64_t insertRecord(const std::string& name, const std::string& email);
    std::optional<Record> readRecord(uint64_t id) const;
    bool updateRecord(uint64_t id, const std::string& name, const std::string& email);
    bool deleteRecord(uint64_t id);
    std::vector<Record> listRecords(bool includeDeleted = false) const;
    bool compact();

private:
    bool execute(const std::string& sql) const;
    bool prepareStatement(const std::string& sql, sqlite3_stmt** stmt) const;
    bool initializeSchema();

    sqlite3* db_;
    std::string path_;
};

std::string formatRecord(const Record& record);

} // namespace db
