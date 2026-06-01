cmake_minimum_required(VERSION 3.15)
project(DBFilehandling LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(SQLITE3_INCLUDE_DIR sqlite3.h HINTS C:/msys64/ucrt64/include)
find_library(SQLITE3_LIBRARY sqlite3 HINTS C:/msys64/ucrt64/lib)
if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR "SQLite3 not found. Please install SQLite3 and ensure the headers and library are available.")
endif()

include_directories(${SQLITE3_INCLUDE_DIR})
add_executable(dbfilehandling src/main.cpp src/DatabaseFile.cpp)
target_link_libraries(dbfilehandling PRIVATE ${SQLITE3_LIBRARY})
