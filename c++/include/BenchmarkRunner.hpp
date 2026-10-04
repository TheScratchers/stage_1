#pragma once

#include <string>
#include <map>
#include <filesystem>

// Executes benchmarks for Datalake hierarchies and Inverted Index structures.
class BenchmarkRunner {
public:
    static void runDatalakeBenchmark(
        const std::filesystem::path& benchDir,
        const std::filesystem::path& datalakePath,
        const std::vector<int>& scales,
        const std::string& outJsonPath
    );

    static void runIndexBenchmark(
        const std::filesystem::path& benchDir,
        const std::filesystem::path& datalakePath,
        const std::vector<int>& scales,
        const std::string& outJsonPath
    );

    static void runMetadataBenchmark(
        const std::filesystem::path& benchDbPath,
        const std::vector<int>& scales,
        const std::string& outJsonPath
    );
};
