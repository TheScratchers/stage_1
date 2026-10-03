cmake_minimum_required(VERSION 3.16)
project(stage1_search_engine VERSION 1.0.0 LANGUAGES CXX)

# ── C++17 Standard ──────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Output binary to build/ ─────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# ── Include paths ───────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/tests)

# ── Compiler flags ──────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

# ── Find dependencies ───────────────────────────────────────
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# ── Source files ────────────────────────────────────────────
set(CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/Datalake.cpp
    ${CMAKE_SOURCE_DIR}/src/MetadataExtractor.cpp
    ${CMAKE_SOURCE_DIR}/src/JsonIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/HierarchicalIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/SqliteIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/QueryEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/ControlLayer.cpp
    ${CMAKE_SOURCE_DIR}/src/Tokenizer.cpp
    ${CMAKE_SOURCE_DIR}/src/BenchmarkRunner.cpp
)

# ── Main Executable ─────────────────────────────────────────
add_executable(search_engine ${CORE_SOURCES} ${CMAKE_SOURCE_DIR}/src/main.cpp)
target_link_libraries(search_engine PRIVATE CURL::libcurl SQLite3::SQLite3)

# ── Automated Unit Tests ────────────────────────────────────
file(GLOB TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/*.cpp")
add_executable(run_tests ${CORE_SOURCES} ${TEST_SOURCES})
target_link_libraries(run_tests PRIVATE CURL::libcurl SQLite3::SQLite3)

enable_testing()
add_test(NAME unit_tests COMMAND run_tests)

message(STATUS "=== Stage 1: C++ Search Engine (C++17) ===")
message(STATUS "Sources   : ${CMAKE_SOURCE_DIR}/src")
message(STATUS "Tests     : ${CMAKE_SOURCE_DIR}/tests")
message(STATUS "Headers   : ${CMAKE_SOURCE_DIR}/include")
message(STATUS "Build Dir : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "CURL      : ${CURL_VERSION_STRING}")
message(STATUS "SQLite3   : ${SQLite3_VERSION}")
