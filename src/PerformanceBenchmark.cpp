#include "PerformanceBenchmark.h"
#include "IndexManager.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>

using Clock = std::chrono::high_resolution_clock;

// Convert a duration to milliseconds as a double.
static double toMs(std::chrono::nanoseconds ns) {
    return std::chrono::duration<double, std::milli>(ns).count();
}

namespace bench {

// ─────────────────────────────────────────────────────────────────────────────
// Static member definitions
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<int> PerformanceBenchmark::DATASET_SIZES = {1000, 10000, 100000, 1000000};
const std::string PerformanceBenchmark::RANGE_LOW    = "Employee_0000001";
const std::string PerformanceBenchmark::RANGE_HIGH   = "Employee_0000100"; // first 100 records
const std::string PerformanceBenchmark::PREFIX_GLOB  = "Employee_0001";    // matches ~1000 records

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
PerformanceBenchmark::PerformanceBenchmark(const std::string& dbPath)
    : dbPath_(dbPath) {}

// ─────────────────────────────────────────────────────────────────────────────
// runAll — iterate over all (algorithm, size) combinations
// ─────────────────────────────────────────────────────────────────────────────
std::vector<AlgoResult> PerformanceBenchmark::runAll() {
    const std::vector<db::AlgorithmType> algos = {
        db::AlgorithmType::LINEAR,
        db::AlgorithmType::HASH,
        db::AlgorithmType::BTREE
    };

    std::vector<AlgoResult> results;
    results.reserve(DATASET_SIZES.size() * algos.size());

    for (int size : DATASET_SIZES) {
        std::cout << "\n=== Dataset size: " << size << " records ===\n";
        for (auto algo : algos) {
            std::cout << "  [" << db::algorithmName(algo) << "] running... " << std::flush;
            AlgoResult r = runOne(algo, size);
            std::cout << "done  (insert=" << std::fixed << std::setprecision(1)
                      << r.insertMs << " ms, lookup=" << r.lookupMs << " ms)\n";
            results.push_back(std::move(r));
        }
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// runOne — benchmark a single (algorithm, dataset-size) pair
// ─────────────────────────────────────────────────────────────────────────────
AlgoResult PerformanceBenchmark::runOne(db::AlgorithmType type, int n) {
    AlgoResult result;
    result.algoName    = db::algorithmName(type);
    result.datasetSize = n;
    result.lookupCount = std::min(LOOKUP_COUNT, n);
    result.updateCount = std::min(UPDATE_COUNT, n);
    result.deleteCount = std::min(DELETE_COUNT, n);

    db::DatabaseFile database;
    if (!database.open(dbPath_)) {
        std::cerr << "Failed to open database: " << dbPath_ << "\n";
        return result;
    }

    // Start clean for this algorithm's table
    database.clearTable(type);

    // Application-level hash index (used only for AlgorithmType::HASH)
    db::IndexManager hashIdx;

    // Track current name for each id (needed for hash-index maintenance during updates)
    std::unordered_map<uint64_t, std::string> idToCurrentName;

    // ── 1. INSERT ──────────────────────────────────────────────────────────
    std::vector<uint64_t>    ids;
    std::vector<std::string> names;
    ids.reserve(static_cast<std::size_t>(n));
    names.reserve(static_cast<std::size_t>(n));

    auto t0 = Clock::now();
    database.beginTransaction();
    for (int i = 1; i <= n; ++i) {
        // Zero-padded 7-digit name: makes lex order == numeric order for clean range queries.
        char nameBuf[32];
        std::snprintf(nameBuf, sizeof(nameBuf), "Employee_%07d", i);
        std::string name  = nameBuf;
        std::string email = std::string("emp") + (nameBuf + 9) + "@company.com";
        uint64_t id = database.insertRecord(type, name, email);
        ids.push_back(id);
        names.push_back(name);
        if (type == db::AlgorithmType::HASH) {
            hashIdx.insert(name, id);
            idToCurrentName[id] = name;
        }
    }
    database.commitTransaction();
    result.insertMs = toMs(Clock::now() - t0);

    // ── Prepare random index pools (deterministic seed = 42) ───────────────
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, n - 1);

    auto randomIndices = [&](int count) {
        std::vector<int> v(static_cast<std::size_t>(count));
        std::generate(v.begin(), v.end(), [&] { return dist(rng); });
        return v;
    };

    const auto lookupIdx = randomIndices(result.lookupCount);
    const auto updateIdx = randomIndices(result.updateCount);

    // Unique delete indices to avoid double-deleting the same record
    std::vector<int> pool(static_cast<std::size_t>(n));
    std::iota(pool.begin(), pool.end(), 0);
    std::shuffle(pool.begin(), pool.end(), rng);
    const std::vector<int> deleteIdx(pool.begin(),
                                     pool.begin() + result.deleteCount);

    // ── 2. LOOKUP (point-lookup by name) ───────────────────────────────────
    // Linear  → WHERE name = ?  with no index  → O(n) full scan
    // B-Tree  → WHERE name = ?  with SQLite index → O(log n)
    // Hash    → unordered_map::find → O(1) avg, then readRecord by rowid → O(1)
    t0 = Clock::now();
    for (int idx : lookupIdx) {
        const std::string& name = names[static_cast<std::size_t>(idx)];
        if (type == db::AlgorithmType::HASH) {
            auto rowid = hashIdx.find(name);
            if (rowid) database.readRecord(type, *rowid);
        } else {
            database.readRecordByName(type, name);
        }
    }
    result.lookupMs = toMs(Clock::now() - t0);

    // ── 3. RANGE QUERY ─────────────────────────────────────────────────────
    // Fetch records where name >= RANGE_LOW AND name <= RANGE_HIGH.
    // B-Tree: O(log n + k). Linear/Hash: O(n) full scan. k = 100 records.
    t0 = Clock::now();
    auto rangeRecs = database.rangeQuery(type, RANGE_LOW, RANGE_HIGH);
    result.rangeQueryMs = toMs(Clock::now() - t0);
    result.rangeCount   = static_cast<int>(rangeRecs.size());

    // ── 4. PREFIX SEARCH ───────────────────────────────────────────────────
    // GLOB 'Employee_0001*'. B-Tree uses index (index-friendly GLOB).
    // Linear/Hash: full table scan. Matches ~1000 records at n >= 2000.
    t0 = Clock::now();
    auto prefixRecs = database.prefixSearch(type, PREFIX_GLOB);
    result.prefixSearchMs = toMs(Clock::now() - t0);
    result.prefixCount    = static_cast<int>(prefixRecs.size());

    // ── 5. ORDERED SCAN BY NAME ────────────────────────────────────────────
    // SELECT ... ORDER BY name.
    // B-Tree: index traversal in sorted order, no filesort — O(n).
    // Linear/Hash: full scan + O(n log n) filesort.
    t0 = Clock::now();
    database.orderedScanByName(type);
    result.orderedScanMs = toMs(Clock::now() - t0);

    t0 = Clock::now();
    database.beginTransaction();
    for (int idx : updateIdx) {
        uint64_t    id       = ids[static_cast<std::size_t>(idx)];
        std::string newName  = "Updated_" + std::to_string(id);
        std::string newEmail = "updated" + std::to_string(id) + "@company.com";
        database.updateRecord(type, id, newName, newEmail);
        if (type == db::AlgorithmType::HASH) {
            const std::string& oldName = idToCurrentName[id];
            hashIdx.remove(oldName);
            hashIdx.insert(newName, id);
            idToCurrentName[id] = newName;
        }
    }
    database.commitTransaction();
    result.updateMs = toMs(Clock::now() - t0);

    // ── 7. DELETE (soft-delete: deleted = 1) ──────────────────────────────
    t0 = Clock::now();
    database.beginTransaction();
    for (int idx : deleteIdx) {
        uint64_t id = ids[static_cast<std::size_t>(idx)];
        database.deleteRecord(type, id);
        if (type == db::AlgorithmType::HASH) {
            const std::string& name = idToCurrentName[id];
            hashIdx.remove(name);
            idToCurrentName.erase(id);
        }
    }
    database.commitTransaction();
    result.deleteMs = toMs(Clock::now() - t0);

    // ── 8. RANGE SCAN (list all non-deleted) ──────────────────────────────
    t0 = Clock::now();
    database.listRecords(type, false);
    result.rangeScanMs = toMs(Clock::now() - t0);

    // ── 9. COMPACT ────────────────────────────────────────────────────────
    t0 = Clock::now();
    database.compact();
    result.compactMs = toMs(Clock::now() - t0);

    result.dbFileSizeBytes = database.getFileSize();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Report generation helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string rpad(const std::string& s, int w) {
    if (static_cast<int>(s.size()) >= w) return s.substr(0, static_cast<std::size_t>(w));
    return s + std::string(static_cast<std::size_t>(w - static_cast<int>(s.size())), ' ');
}

std::string lpad(const std::string& s, int w) {
    if (static_cast<int>(s.size()) >= w) return s.substr(0, static_cast<std::size_t>(w));
    return std::string(static_cast<std::size_t>(w - static_cast<int>(s.size())), ' ') + s;
}

std::string sepLine(char fill, int width) {
    return std::string(static_cast<std::size_t>(width), fill);
}

std::string fmtMs(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3) << v << " ms";
    return ss.str();
}

std::string fmtMsWinner(double v, double best) {
    return fmtMs(v) + (v == best ? " (*)" : "    ");
}

std::string fmtOpsPerSec(double ops, double ms) {
    if (ms <= 0.0) return "  N/A      ";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(0)
       << (ops / (ms / 1000.0));
    return ss.str();
}

std::string fmtKB(uint64_t bytes) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
    return ss.str();
}

} // anon namespace

// ─────────────────────────────────────────────────────────────────────────────
// generateReport
// ─────────────────────────────────────────────────────────────────────────────
std::string PerformanceBenchmark::generateReport(const std::vector<AlgoResult>& results) {
    std::ostringstream out;

    // ── Banner ───────────────────────────────────────────────────────────────
    out <<
        "\n"
        "================================================================================\n"
        "        DATABASE INDEXING ALGORITHM  —  PERFORMANCE BENCHMARK REPORT\n"
        "        Linear Scan  vs  Hash Index (unordered_map)  vs  B-Tree Index\n"
        "================================================================================\n\n";

    // ── Algorithm descriptions ────────────────────────────────────────────────
    out <<
        "ALGORITHMS\n"
        "----------\n"
        "  Linear Scan   No SQLite index on 'name'.  Every name-based lookup\n"
        "                performs a full sequential table scan.  Complexity: O(n).\n\n"
        "  Hash Index    No SQLite index.  An in-memory std::unordered_map<name,rowid>\n"
        "                is maintained by the application.  Lookup is O(1) average;\n"
        "                inserts and deletes also update the map.\n\n"
        "  B-Tree Index  Native SQLite CREATE INDEX on the 'name' column.\n"
        "                SQLite automatically uses the B-Tree for WHERE name=? queries.\n"
        "                Complexity: O(log n) lookup, O(log n) insert overhead.\n\n";

    // ── Methodology ──────────────────────────────────────────────────────────
    out <<
        "METHODOLOGY\n"
        "-----------\n"
        "  * Each algorithm operates on its own isolated table.\n"
        "  * Database opened with WAL journal mode, NORMAL sync, 16 MB page cache.\n"
        "  * Inserts wrapped in explicit BEGIN/COMMIT transactions.\n"
        "  * Updates and deletes also transacted to isolate their cost.\n"
        "  * Lookup  : " << LOOKUP_COUNT << " point-lookups by name (random, seed=42).\n"
        "  * Range Query : name BETWEEN low AND high (matches 100 records).\n"
        "  * Prefix Search : name GLOB 'Employee_0001*' (matches ~1000 records at large scales).\n"
        "  * Ordered Scan : SELECT all non-deleted records ORDER BY name.\n"
        "  * Update  : " << UPDATE_COUNT << " records updated by primary-key id.\n"
        "  * Delete  : " << DELETE_COUNT << " unique soft-deletes (deleted=1).\n"
        "  * Range Scan: SELECT all non-deleted records in id order.\n"
        "  * Compact : VACUUM the entire database.\n"
        "  * (*) marks the fastest algorithm for each operation.\n\n";

    // ── Per-size tables ───────────────────────────────────────────────────────
    // Column widths
    const int C0 = 26, C1 = 18, C2 = 18, C3 = 18;
    const int totalW = C0 + C1 + C2 + C3;

    // Collect unique sizes in sorted order
    std::vector<int> sizes;
    for (auto& r : results)
        if (std::find(sizes.begin(), sizes.end(), r.datasetSize) == sizes.end())
            sizes.push_back(r.datasetSize);
    std::sort(sizes.begin(), sizes.end());

    for (int sz : sizes) {
        // Gather the three results for this size
        const AlgoResult* lin  = nullptr;
        const AlgoResult* hsh  = nullptr;
        const AlgoResult* btr  = nullptr;
        for (auto& r : results) {
            if (r.datasetSize != sz) continue;
            if (r.algoName == "Linear Scan")  lin  = &r;
            if (r.algoName == "Hash Index")   hsh  = &r;
            if (r.algoName == "B-Tree Index") btr  = &r;
        }
        if (!lin || !hsh || !btr) continue;

        // Table header
        out << sepLine('=', totalW) << "\n"
            << " DATASET SIZE: " << sz << " RECORDS\n"
            << sepLine('=', totalW) << "\n\n";

        out << " " << rpad("Operation", C0)
            << lpad("Linear Scan", C1)
            << lpad("Hash Index", C2)
            << lpad("B-Tree Index", C3) << "\n";
        out << " " << sepLine('-', totalW - 1) << "\n";

        // Convenience macro-style lambda for one timing row
        auto row = [&](const std::string& label, double lv, double hv, double bv) {
            double best = std::min({lv, hv, bv});
            out << " " << rpad(label, C0)
                << lpad(fmtMsWinner(lv, best), C1)
                << lpad(fmtMsWinner(hv, best), C2)
                << lpad(fmtMsWinner(bv, best), C3) << "\n";
        };

        row("Insert " + std::to_string(sz) + " records",
            lin->insertMs, hsh->insertMs, btr->insertMs);
        row("Lookup (" + std::to_string(LOOKUP_COUNT) + " by name)",
            lin->lookupMs, hsh->lookupMs, btr->lookupMs);
        row("Range Query (100 records)",
            lin->rangeQueryMs, hsh->rangeQueryMs, btr->rangeQueryMs);
        row("Prefix Search (GLOB)",
            lin->prefixSearchMs, hsh->prefixSearchMs, btr->prefixSearchMs);
        row("Ordered Scan (ORDER BY)",
            lin->orderedScanMs, hsh->orderedScanMs, btr->orderedScanMs);
        row("Update (" + std::to_string(UPDATE_COUNT) + " records)",
            lin->updateMs, hsh->updateMs, btr->updateMs);
        row("Delete (" + std::to_string(DELETE_COUNT) + " soft)",
            lin->deleteMs, hsh->deleteMs, btr->deleteMs);
        row("Range Scan (list all)",
            lin->rangeScanMs, hsh->rangeScanMs, btr->rangeScanMs);
        row("Compact (VACUUM)",
            lin->compactMs, hsh->compactMs, btr->compactMs);

        out << " " << sepLine('-', totalW - 1) << "\n";

        // Throughput section
        out << "\n Throughput:\n";
        out << "   Insert (rec/s)  : "
            << lpad(fmtOpsPerSec(sz,   lin->insertMs), 12) << " (Linear) | "
            << lpad(fmtOpsPerSec(sz,   hsh->insertMs), 12) << " (Hash)   | "
            << lpad(fmtOpsPerSec(sz,   btr->insertMs), 12) << " (B-Tree)\n";
        out << "   Lookup (op/s)   : "
            << lpad(fmtOpsPerSec(LOOKUP_COUNT, lin->lookupMs), 12) << " (Linear) | "
            << lpad(fmtOpsPerSec(LOOKUP_COUNT, hsh->lookupMs), 12) << " (Hash)   | "
            << lpad(fmtOpsPerSec(LOOKUP_COUNT, btr->lookupMs), 12) << " (B-Tree)\n";

        // File size (shared database, reported per run)
        out << "\n DB file size after compact: "
            << fmtKB(lin->dbFileSizeBytes) << "\n";

        // Winner summary
        auto winnerOf = [](double l, double h, double b) -> const char* {
            double best = std::min({l, h, b});
            if (best == l) return "Linear Scan";
            if (best == h) return "Hash Index";
            return "B-Tree Index";
        };

        out << "\n Winner per operation:\n"
            << "   Insert       : " << winnerOf(lin->insertMs,       hsh->insertMs,       btr->insertMs)       << "\n"
            << "   Lookup       : " << winnerOf(lin->lookupMs,       hsh->lookupMs,       btr->lookupMs)       << "\n"
            << "   Range Query  : " << winnerOf(lin->rangeQueryMs,   hsh->rangeQueryMs,   btr->rangeQueryMs)   << "\n"
            << "   Prefix Search: " << winnerOf(lin->prefixSearchMs, hsh->prefixSearchMs, btr->prefixSearchMs) << "\n"
            << "   Ordered Scan : " << winnerOf(lin->orderedScanMs,  hsh->orderedScanMs,  btr->orderedScanMs)  << "\n"
            << "   Update       : " << winnerOf(lin->updateMs,       hsh->updateMs,       btr->updateMs)       << "\n"
            << "   Delete       : " << winnerOf(lin->deleteMs,       hsh->deleteMs,       btr->deleteMs)       << "\n"
            << "   Range Scan   : " << winnerOf(lin->rangeScanMs,    hsh->rangeScanMs,    btr->rangeScanMs)    << "\n"
            << "   Compact      : " << winnerOf(lin->compactMs,      hsh->compactMs,      btr->compactMs)      << "\n";

        out << "\n";
    }

    // ── Scaling analysis ─────────────────────────────────────────────────────
    if (sizes.size() >= 2) {
        out << sepLine('=', totalW) << "\n"
            << " SCALING ANALYSIS  (lookup latency across dataset sizes)\n"
            << sepLine('=', totalW) << "\n\n";

        out << " " << rpad("Algorithm", 16);
        for (int sz : sizes) out << lpad(std::to_string(sz) + " rows", 18);
        out << "\n " << sepLine('-', 16 + 18 * static_cast<int>(sizes.size())) << "\n";

        for (const char* algo : {"Linear Scan", "Hash Index", "B-Tree Index"}) {
            out << " " << rpad(algo, 16);
            for (int sz : sizes) {
                for (auto& r : results) {
                    if (r.datasetSize == sz && r.algoName == algo) {
                        out << lpad(fmtMs(r.lookupMs), 18);
                        break;
                    }
                }
            }
            out << "\n";
        }

        out << "\n"
            << " Linear Scan latency grows proportionally with table size (O(n)).\n"
            << " Hash Index lookup stays near-constant regardless of size  (O(1) avg).\n"
            << " B-Tree Index grows logarithmically — barely visible at these scales.\n\n";
    }

    // ── Conclusions ──────────────────────────────────────────────────────────
    out << sepLine('=', totalW) << "\n"
        << " ANALYSIS & RECOMMENDATIONS\n"
        << sepLine('=', totalW) << "\n\n"

        << " 1. INSERT PERFORMANCE\n"
        << "    All strategies issue inserts into SQLite pages inside transactions.\n"
        << "    B-Tree index adds overhead per insert to maintain the sorted index tree.\n"
        << "    Hash strategy maintains the in-memory map (negligible cost vs. disk I/O).\n"
        << "    Expected: Linear (fastest) ~= Hash < B-Tree (index maintenance overhead).\n\n"

        << " 2. POINT LOOKUP BY NAME\n"
        << "    Hash achieves O(1) average via unordered_map, then O(1) by primary key.\n"
        << "    B-Tree achieves O(log n) via the SQLite native index.\n"
        << "    Linear performs O(n) full table scan — degrades severely with data size.\n"
        << "    Expected: Hash < B-Tree << Linear (gap widens as n increases).\n\n"

        << " 3. UPDATE PERFORMANCE\n"
        << "    Updates target a known primary key (O(1) SQLite row access).\n"
        << "    B-Tree must update its index entry for the changed 'name' field.\n"
        << "    Hash must update the in-memory map keys on name change.\n"
        << "    Expected: similar for all three; minor overhead in Hash and B-Tree.\n\n"

        << " 4. B-TREE ADVANTAGE (Range, Prefix, Ordering)\n"
        << "    Range Query: B-Tree uses index to jump to start and scan exactly k rows (O(log n + k)).\n"
        << "    Prefix Search: B-Tree uses index for GLOB matches (O(log n + k)).\n"
        << "    Ordered Scan: B-Tree avoids O(n log n) filesort by traversing index.\n"
        << "    Linear & Hash: Force a full table scan O(n) for all three, plus filesort for ordering.\n"
        << "    Expected: B-Tree Dominates << Hash ~= Linear.\n\n"

        << " 5. RANGE SCAN (No Order)\n"
        << "    All strategies read the full active record set sequentially — O(n).\n"
        << "    No meaningful difference expected between strategies.\n\n"

        << " 6. SCALABILITY SUMMARY\n"
        << "    As n grows from 1K to 50K, Linear lookup degrades ~50x.\n"
        << "    Hash lookup stays nearly constant (in-memory O(1)).\n"
        << "    B-Tree lookup grows logarithmically (~log2(50000/1000) = ~5.6x slower).\n\n"

        << " RECOMMENDATIONS\n"
        << "    - Read-heavy workloads, large datasets : Use B-Tree indexes (balanced).\n"
        << "    - Lookup-dominant, memory available    : Use Hash index for max speed.\n"
        << "    - Range queries, sorting by column     : Use B-Tree indexes (essential).\n"
        << "    - Write-heavy, no point lookups needed : Linear (no index overhead).\n"
        << "    - Never leave large tables un-indexed  : Linear degrades rapidly.\n\n"

        << " NOTE: Timings depend on disk speed, OS, CPU cache, and SQLite page cache.\n"
        << " For production analysis, run multiple passes and average the results.\n"
        << " (*) marks the fastest algorithm in each row.\n";

    return out.str();
}

} // namespace bench
