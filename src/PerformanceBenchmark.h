#pragma once

#include "DatabaseFile.h"
#include <string>
#include <vector>

namespace bench {

// ─────────────────────────────────────────────────────────────────────────────
// Result of one (algorithm, dataset-size) benchmark run
// ─────────────────────────────────────────────────────────────────────────────
struct AlgoResult {
    std::string algoName;
    int         datasetSize  = 0;

    // Timings (milliseconds)
    double insertMs      = 0.0;   ///< Bulk insert (n records, transacted)
    double lookupMs      = 0.0;   ///< LOOKUP_COUNT point-lookups by name
    double rangeQueryMs  = 0.0;   ///< Range query: name BETWEEN low AND high  [B-Tree advantage]
    double prefixSearchMs= 0.0;   ///< Prefix GLOB search                      [B-Tree advantage]
    double orderedScanMs = 0.0;   ///< SELECT ... ORDER BY name                 [B-Tree advantage]
    double updateMs      = 0.0;   ///< UPDATE_COUNT record updates (transacted)
    double deleteMs      = 0.0;   ///< DELETE_COUNT soft-deletes (transacted)
    double rangeScanMs   = 0.0;   ///< Full non-deleted list (by id)
    double compactMs     = 0.0;   ///< VACUUM

    uint64_t dbFileSizeBytes = 0; ///< File size after compact

    // Counts (may be < the constants when n is small)
    int lookupCount = 0;
    int updateCount = 0;
    int deleteCount = 0;
    int rangeCount  = 0;          ///< Records returned by range query
    int prefixCount = 0;          ///< Records returned by prefix search
};

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark harness
// ─────────────────────────────────────────────────────────────────────────────
class PerformanceBenchmark {
public:
    explicit PerformanceBenchmark(const std::string& dbPath);

    /// Run all combinations: 3 algorithms × 3 dataset sizes.
    std::vector<AlgoResult> runAll();

    /// Render a rich ASCII report from a result set.
    static std::string generateReport(const std::vector<AlgoResult>& results);

    // Dataset sizes benchmarked
    static const std::vector<int> DATASET_SIZES;

    // Per-run operation counts
    static constexpr int LOOKUP_COUNT = 100;
    static constexpr int UPDATE_COUNT = 100;
    static constexpr int DELETE_COUNT =  50;

    // B-Tree advantage benchmark parameters
    // Range covers the first 100 zero-padded records — a small fraction at large n.
    static const std::string RANGE_LOW;    ///< "Employee_0000001"
    static const std::string RANGE_HIGH;   ///< "Employee_0000100"
    static const std::string PREFIX_GLOB;  ///< "Employee_0001" → matches ~1000 records

private:
    AlgoResult runOne(db::AlgorithmType type, int n);

    std::string dbPath_;
};

} // namespace bench
