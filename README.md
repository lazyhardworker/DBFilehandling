# CDB1(Custom Database Binary format v1)

A C++17 project that benchmarks **three database indexing strategies** — Linear Scan, Hash Index, and B-Tree Index — against a real SQLite database, then generates a detailed performance analysis report.

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Architecture](#architecture)
3. [Algorithm Descriptions](#algorithm-descriptions)
4. [File Structure](#file-structure)
5. [Build Instructions](#build-instructions)
6. [Running the Benchmark](#running-the-benchmark)
7. [Understanding the Report](#understanding-the-report)
8. [Performance Characteristics](#performance-characteristics)
9. [API Reference](#api-reference)
10. [Benchmark Methodology](#benchmark-methodology)
11. [Dependencies](#dependencies)

---

## Project Overview

DBFilehandling demonstrates how the choice of **indexing strategy** affects SQLite database performance across all major operations — insert, lookup, update, delete, range scan, and compaction.

Three strategies are benchmarked at three dataset sizes (1 000, 10 000, 50 000 records):

| Strategy | Description | Lookup Complexity |
|---|---|---|
| **Linear Scan** | No index. SQLite does a full sequential table scan for every lookup. | O(n) |
| **Hash Index** | Application-level `std::unordered_map<name, rowid>`. Lookup hits memory first, then fetches by primary key. | O(1) avg |
| **B-Tree Index** | Native SQLite `CREATE INDEX` on the `name` column. SQLite uses the index automatically for `WHERE name = ?`. | O(log n) |

The program produces `performance_report.txt` with timing tables, throughput metrics, a scaling analysis, and actionable recommendations.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      main.cpp                           │
│  Creates DB → runs PerformanceBenchmark → writes report │
└────────────────────┬────────────────────────────────────┘
                     │
         ┌───────────▼───────────┐
         │  PerformanceBenchmark │   Orchestrates all (algo × size) runs
         │  PerformanceBenchmark │   Generates the ASCII report
         └─────┬──────────┬──────┘
               │          │
    ┌──────────▼──┐  ┌────▼──────────┐
    │ DatabaseFile│  │ IndexManager  │
    │             │  │               │
    │ employees_  │  │ unordered_map │
    │  linear     │  │  name→rowid   │
    │  hash       │  │ (HASH only)   │
    │  btree      │  └───────────────┘
    │             │
    │   SQLite    │
    └─────────────┘
```

Each algorithm gets its own **isolated SQLite table** so all three can be benchmarked in the same database session without interference.

---

## Algorithm Descriptions

### 1. Linear Scan (`employees_linear`)

```sql
-- No extra index. Every lookup is:
SELECT id, name, email FROM employees_linear WHERE name = ? AND deleted = 0 LIMIT 1;
-- SQLite does a full sequential scan — reads every page until it finds a match.
```

- **Complexity**: O(n) per lookup.
- **Insert overhead**: None (no index to maintain).
- **Best for**: Write-heavy workloads with rare point queries, or tiny tables (< 500 rows).

### 2. Hash Index (`employees_hash` + `IndexManager`)

```
Application memory:
  unordered_map<string, uint64_t>
  { "Employee_1" → 1, "Employee_2" → 2, … }

Lookup flow:
  1. hashMap.find(name) → rowid    [O(1) average, in RAM]
  2. SELECT … WHERE id = rowid     [O(1) by primary key]
```

- **Complexity**: O(1) average lookup.
- **Insert overhead**: One `unordered_map::insert` per row.
- **Caveat**: Entire index must fit in RAM; rebuilt from DB on re-open.
- **Best for**: Read-heavy lookup workloads where the key set fits in memory.

### 3. B-Tree Index (`employees_btree`)

```sql
-- Created once at schema init:
CREATE INDEX IF NOT EXISTS idx_btree_name ON employees_btree(name);

-- SQLite automatically uses the index for:
SELECT … FROM employees_btree WHERE name = ? LIMIT 1;
-- Lookup traverses the B-Tree: O(log n) comparisons.
```

- **Complexity**: O(log n) lookup.
- **Insert overhead**: One B-Tree insertion per row to maintain the index.
- **Best for**: General-purpose production workloads; balanced read/write performance that scales to millions of rows.

---

## File Structure

```
DBFilehandling/
├── src/
│   ├── main.cpp                  Entry point — drives the benchmark
│   ├── DatabaseFile.h/.cpp       SQLite wrapper with 3-table schema & CRUD
│   ├── IndexManager.h/.cpp       In-memory hash index (unordered_map)
│   ├── PerformanceBenchmark.h    AlgoResult struct + harness declaration
│   └── PerformanceBenchmark.cpp  Benchmark runner + ASCII report generator
├── CMakeLists.txt                CMake build file
├── README.md                     This file
├── database.db                   Generated SQLite database (3 tables)
└── performance_report.txt        Generated benchmark report
```

---

## Build Instructions

### Option A — g++ directly (MSYS2 / MinGW)

```powershell
g++ -std=c++17 -O2 `
    -IC:\msys64\ucrt64\include `
    src/main.cpp src/DatabaseFile.cpp src/IndexManager.cpp src/PerformanceBenchmark.cpp `
    -LC:\msys64\ucrt64\lib -lsqlite3 `
    -o dbfilehandling.exe
```

### Option B — CMake

```powershell
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

> **Prerequisites**: MSYS2 with the `ucrt64` toolchain and `mingw-w64-ucrt-x86_64-sqlite3` package installed.
>
> Install if needed:
> ```bash
> pacman -S mingw-w64-ucrt-x86_64-sqlite3
> ```

---

## Running the Benchmark

```powershell
.\dbfilehandling.exe
```

The program will:

1. Create (or overwrite) `database.db` with three tables.
2. Run all 9 benchmark combinations (3 algorithms × 3 dataset sizes).
3. Print a rich performance report to the terminal.
4. Save the same report to `performance_report.txt`.

**Expected runtime**: ~10–30 seconds depending on disk speed.

---

## Understanding the Report

### Timing Table

Each dataset-size section shows a comparison table:

```
 Operation                        Linear Scan        Hash Index      B-Tree Index
 -------------------------------------------------------------------------------
 Insert 10000 records           14.614 ms (*)     16.202 ms         18.309 ms
 Lookup (100 by name)            8.325 ms          0.410 ms (*)      0.471 ms
 ...
```

- `(*)` marks the **fastest algorithm** for each row.
- All lookups use `name` as the search key — the most discriminating operation for index comparison.
- Inserts, updates, and deletes are wrapped in `BEGIN/COMMIT` transactions for accuracy and realism.

### Throughput Section

```
 Throughput:
   Insert (rec/s)  :       684294 (Linear) |       617208 (Hash) | ...
   Lookup (op/s)   :        12012 (Linear) |       244021 (Hash) | ...
```

Derived from `operations / (time_ms / 1000)`.

### Scaling Analysis

Shows how lookup latency changes as the dataset grows from 1K → 10K → 50K:

```
 Algorithm         1000 rows    10000 rows    50000 rows
 --------------------------------------------------------
 Linear Scan        1.185 ms     8.325 ms     42.430 ms   ← grows ~linearly
 Hash Index         0.327 ms     0.410 ms      0.505 ms   ← nearly flat
 B-Tree Index       0.433 ms     0.471 ms      0.675 ms   ← logarithmic growth
```

---

## Performance Characteristics

### Theoretical Complexity

| Operation | Linear Scan | Hash Index | B-Tree Index |
|---|---|---|---|
| Insert | O(1) | O(1) amortized | O(log n) |
| Lookup by name | **O(n)** | **O(1) avg** | **O(log n)** |
| Lookup by ID | O(1) | O(1) | O(1) |
| Update (by ID) | O(1) | O(1) + map | O(1) + tree |
| Delete (soft) | O(1) | O(1) + map | O(1) + tree |
| Range Scan | O(n) | O(n) | O(n) |

### Observed Results (sample from benchmark run)

| Dataset | Lookup — Linear | Lookup — Hash | Lookup — B-Tree |
|---|---|---|---|
| 1 000 rows | 1.185 ms | 0.327 ms | 0.433 ms |
| 10 000 rows | 8.325 ms | 0.410 ms | 0.471 ms |
| 50 000 rows | 42.430 ms | 0.505 ms | 0.675 ms |

**Takeaway**: At 50K rows, Hash lookup is **~84× faster** than Linear, and B-Tree is **~63× faster** than Linear.

---

## API Reference

### `db::AlgorithmType` (enum)

```cpp
enum class AlgorithmType { LINEAR, HASH, BTREE };
```

### `db::DatabaseFile`

| Method | Description |
|---|---|
| `create(path)` | Delete existing DB, create fresh. |
| `open(path)` | Open or create DB; initialises all three schemas. |
| `close()` | Close SQLite handle. |
| `beginTransaction()` | Issue `BEGIN TRANSACTION`. |
| `commitTransaction()` | Issue `COMMIT`. |
| `rollbackTransaction()` | Issue `ROLLBACK`. |
| `insertRecord(type, name, email)` | Insert into the algorithm's table; returns new rowid. |
| `readRecord(type, id)` | Fetch by primary key. |
| `readRecordByName(type, name)` | Fetch by name (uses index or full scan). |
| `updateRecord(type, id, name, email)` | Update non-deleted record by ID. |
| `deleteRecord(type, id)` | Soft-delete (`deleted = 1`). |
| `listRecords(type, includeDeleted)` | Return all (or non-deleted) records. |
| `clearTable(type)` | `DELETE FROM` the algorithm's table. |
| `compact()` | `VACUUM` the entire database. |
| `getFileSize()` | File size in bytes. |

### `db::IndexManager`

```cpp
void                    insert(key, rowid);
std::optional<uint64_t> find(key);
void                    remove(key);
void                    clear();
std::size_t             size();
```

### `bench::PerformanceBenchmark`

```cpp
PerformanceBenchmark bm("database.db");
auto results = bm.runAll();
std::string report = PerformanceBenchmark::generateReport(results);
```

### `bench::AlgoResult` fields

```cpp
std::string algoName;
int         datasetSize;
double      insertMs, lookupMs, updateMs, deleteMs, rangeScanMs, compactMs;
uint64_t    dbFileSizeBytes;
int         lookupCount, updateCount, deleteCount;
```

---

## Benchmark Methodology

- **Isolation**: Each algorithm uses its own table (`employees_linear`, `employees_hash`, `employees_btree`). Tables are cleared (`DELETE FROM`) before each run for a clean state.
- **Transactions**: Bulk inserts, updates, and deletes are wrapped in `BEGIN/COMMIT` blocks. This reflects realistic application usage and removes per-statement transaction overhead from timing.
- **Reproducibility**: Random record selection uses a fixed seed (`std::mt19937(42)`), so results are deterministic across runs.
- **SQLite settings**: WAL journal mode, `NORMAL` synchronous, 16 MB page cache — reasonable production-like settings.
- **Soft deletes**: Deletions set `deleted = 1` rather than removing rows. This is realistic for many applications and ensures VACUUM has work to do.
- **Timing**: Wall-clock time via `std::chrono::high_resolution_clock`.

---

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| SQLite3 | ≥ 3.35 | Embedded relational database |
| C++ Standard Library | C++17 | `std::filesystem`, `std::optional`, `std::unordered_map` |

> SQLite is linked as a shared library from the MSYS2 `ucrt64` toolchain.
> The database file (`database.db`) is a standard SQLite3 file — it can be opened with any SQLite browser (e.g., [DB Browser for SQLite](https://sqlitebrowser.org/)).
