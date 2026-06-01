# DBFilehandling

Simple C++ database example using SQLite.

This project creates `database.db` as a real SQLite database file.

## Build

Use CMake or a compiler directly.

Example:

```powershell
mkdir build
cd build
cmake ..
cmake --build .
.\dbfilehandling.exe
```

Or with `g++` directly (if SQLite is installed):

```powershell
g++ -std=c++17 src/main.cpp src/DatabaseFile.cpp -lsqlite3 -o dbfilehandling.exe
```

## Performance report

Running the program now generates `performance_report.txt` alongside `database.db`. It includes timed metrics for create, insert, read, update, delete, compact, and list operations.

## Notes

- `database.db` is a standard SQLite file.
- You can open it with SQLite viewers, including web-based SQLite viewers.
