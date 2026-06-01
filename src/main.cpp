#include "DatabaseFile.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>

using Clock = std::chrono::high_resolution_clock;

static double measureDuration(std::function<void()> action) {
    const auto start = Clock::now();
    action();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static void writeReport(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << content;
    }
}

int main() {
    const std::string path = "database.db";
    const std::string reportPath = "performance_report.txt";
    db::DatabaseFile db;

    std::vector<uint64_t> ids;
    double createTime = measureDuration([&] {
        if (!db.create(path)) {
            throw std::runtime_error("Failed to create database file: " + path);
        }
    });

    double insertTime = measureDuration([&] {
        ids.reserve(100);
        for (int i = 1; i <= 100; ++i) {
            std::string name = "Employee " + std::to_string(i);
            std::string email = "employee" + std::to_string(i) + "@example.com";
            uint64_t id = db.insertRecord(name, email);
            if (id == 0) {
                throw std::runtime_error("Failed to insert employee " + std::to_string(i));
            }
            ids.push_back(id);
        }
    });

    double readTime = measureDuration([&] {
        for (auto id : ids) {
            auto record = db.readRecord(id);
            if (!record) {
                throw std::runtime_error("Failed to read record " + std::to_string(id));
            }
        }
    });

    double updateTime = measureDuration([&] {
        for (auto id : ids) {
            std::string name = "Employee " + std::to_string(id) + " Updated";
            std::string email = "employee" + std::to_string(id) + ".updated@example.com";
            if (!db.updateRecord(id, name, email)) {
                throw std::runtime_error("Failed to update record " + std::to_string(id));
            }
        }
    });

    std::vector<uint64_t> deletedIds(ids.begin(), ids.begin() + 10);
    double deleteTime = measureDuration([&] {
        for (auto id : deletedIds) {
            if (!db.deleteRecord(id)) {
                throw std::runtime_error("Failed to delete record " + std::to_string(id));
            }
        }
    });

    double compactTime = measureDuration([&] {
        if (!db.compact()) {
            throw std::runtime_error("Failed to compact database");
        }
    });

    double listTime = measureDuration([&] {
        db.listRecords();
    });

    const auto finalRecords = db.listRecords();
    const uint64_t fileSize = std::filesystem::file_size(path);

    std::ostringstream report;
    report << std::fixed << std::setprecision(3);
    report << "Database Performance Report\n";
    report << "===========================\n";
    report << "Database file: " << path << "\n";
    report << "File size: " << fileSize << " bytes\n";
    report << "Total records inserted: " << ids.size() << "\n";
    report << "Records after delete/compact: " << finalRecords.size() << "\n";
    report << "\n";
    report << "Operation timings (milliseconds):\n";
    report << "- Create database: " << createTime << " ms\n";
    report << "- Insert 100 records: " << insertTime << " ms\n";
    report << "- Read 100 records: " << readTime << " ms\n";
    report << "- Update 100 records: " << updateTime << " ms\n";
    report << "- Delete 10 records: " << deleteTime << " ms\n";
    report << "- Compact database: " << compactTime << " ms\n";
    report << "- List records: " << listTime << " ms\n";
    report << "\n";
    report << "Note: actual timings depend on disk, OS, and hardware.\n";

    writeReport(reportPath, report.str());
    std::cout << report.str();
    std::cout << "Performance report written to " << reportPath << "\n";
    return 0;
}
