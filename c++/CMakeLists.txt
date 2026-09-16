cmake_minimum_required(VERSION 3.16)
project(stage1_search_engine VERSION 0.1.0 LANGUAGES CXX)

# ── C++17 standard ──────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Output binary to build/ ─────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# ── Include paths ───────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── Compiler warnings ───────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ── Collect all source files ────────────────────────────────
file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")

# ── Main executable (added when src/ is populated) ──────────
# add_executable(search_engine ${SOURCES})

message(STATUS "Stage 1 — C++ Search Engine (C++17)")
message(STATUS "Sources directory : ${CMAKE_SOURCE_DIR}/src")
message(STATUS "Headers directory : ${CMAKE_SOURCE_DIR}/include")
message(STATUS "Build output      : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
