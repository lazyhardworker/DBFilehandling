#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <sqlite3.h>

using namespace std;

namespace db {

struct Record {
    uint64_t id;
    bool deleted;
    string name;
    string email;
};

class DatabaseFile {
public:
    DatabaseFile();
    ~DatabaseFile();

    bool create(const string& path);
    bool open(const string& path);
    void close();
    bool isOpen() const;

    uint64_t insertRecord(const string& name, const string& email);
    optional<Record> readRecord(uint64_t id) const;
    bool updateRecord(uint64_t id, const string& name, const string& email);
    bool deleteRecord(uint64_t id);
    vector<Record> listRecords(bool includeDeleted = false) const;
    bool compact();

private:
    bool execute(const string& sql) const;
    bool prepareStatement(const string& sql, sqlite3_stmt** stmt) const;
    bool initializeSchema();

    sqlite3* db_;
    string path_;
};

string formatRecord(const Record& record);

} // namespace db
