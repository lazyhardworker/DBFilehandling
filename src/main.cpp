#include "PerformanceBenchmark.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::trunc);
    if (out) out << content;
    else std::cerr << "Warning: could not write to " << path << "\n";
}

int main() {
    const std::string dbPath     = "database.db";
    const std::string reportPath = "performance_report.txt";

    std::cout << "\n"
              << "================================================================================\n"
              << "  DBFilehandling — Multi-Algorithm SQLite Performance Benchmark\n"
              << "  Algorithms: Linear Scan | Hash Index | B-Tree Index\n"
              << "  Dataset sizes: 1 000 / 10 000 / 50 000 records per algorithm\n"
              << "================================================================================\n";

    // Create a fresh database (removes any previous database.db)
    {
        db::DatabaseFile tmp;
        if (!tmp.create(dbPath)) {
            throw std::runtime_error("Failed to create database: " + dbPath);
        }
        // tmp goes out of scope → closed
    }

    bench::PerformanceBenchmark benchmark(dbPath);
    const auto results = benchmark.runAll();

    const std::string report = bench::PerformanceBenchmark::generateReport(results);

    std::cout << report;
    writeFile(reportPath, report);

    std::cout << "\nPerformance report saved to: " << reportPath << "\n"
              << "Database file            : " << dbPath
              << "  (" << std::filesystem::file_size(dbPath) / 1024 << " KB)\n\n";

    return 0;
}
