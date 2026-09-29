#include "Datalake.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>

namespace fs = std::filesystem;

// Converts layout enum to string.
std::string Datalake::layoutToString(DatalakeLayout layout) {
    switch (layout) {
        case DatalakeLayout::TimeBased:  return "time_based";
        case DatalakeLayout::BookBased:  return "book_based";
        case DatalakeLayout::BatchBased: return "batch_based";
    }
    return "unknown";
}

// Computes the directory path for a given layout strategy.
fs::path Datalake::getDirectoryPath(
    const fs::path& baseDir,
    DatalakeLayout layout,
    int bookId,
    std::chrono::system_clock::time_point timestamp
) {
    switch (layout) {
        case DatalakeLayout::TimeBased: {
            std::time_t timeT = std::chrono::system_clock::to_time_t(timestamp);
            std::tm tmStruct{};
#if defined(_WIN32) || defined(_WIN64)
            localtime_s(&tmStruct, &timeT);
#else
            localtime_r(&timeT, &tmStruct);
#endif
            std::ostringstream oss;
            oss << std::put_time(&tmStruct, "%Y%m%d/%H");
            return baseDir / oss.str();
        }
        case DatalakeLayout::BookBased: {
            return baseDir / std::to_string(bookId);
        }
        case DatalakeLayout::BatchBased: {
            // Group books in batches of 500 (e.g., batch_0, batch_1, etc.)
            int batchNumber = bookId / 500;
            return baseDir / ("batch_" + std::to_string(batchNumber));
        }
    }
    return baseDir;
}

// Writes header and body files to disk under the partition folder.
bool Datalake::saveBook(
    const fs::path& baseDir,
    DatalakeLayout layout,
    int bookId,
    const std::string& headerContent,
    const std::string& bodyContent,
    std::chrono::system_clock::time_point timestamp
) {
    fs::path targetDir = getDirectoryPath(baseDir, layout, bookId, timestamp);
    std::error_code ec;
    fs::create_directories(targetDir, ec);
    if (ec) {
        std::cerr << "[Datalake::saveBook] Failed to create directories: " << targetDir << " - " << ec.message() << "\n";
        return false;
    }

    fs::path headerPath = targetDir / (std::to_string(bookId) + ".header.txt");
    fs::path bodyPath = targetDir / (std::to_string(bookId) + ".body.txt");

    std::ofstream hFile(headerPath, std::ios::binary);
    if (!hFile.is_open()) return false;
    hFile.write(headerContent.data(), headerContent.size());

    std::ofstream bFile(bodyPath, std::ios::binary);
    if (!bFile.is_open()) return false;
    bFile.write(bodyContent.data(), bodyContent.size());

    return true;
}

// Locates a book's files on disk using direct path resolution when possible.
BookFiles Datalake::locateBook(
    const fs::path& baseDir,
    DatalakeLayout layout,
    int bookId
) {
    BookFiles files;
    if (layout == DatalakeLayout::BookBased || layout == DatalakeLayout::BatchBased) {
        fs::path dir = getDirectoryPath(baseDir, layout, bookId);
        files.headerPath = dir / (std::to_string(bookId) + ".header.txt");
        files.bodyPath = dir / (std::to_string(bookId) + ".body.txt");
        files.exists = fs::exists(files.headerPath) && fs::exists(files.bodyPath);
        if (files.exists) return files;
    }

    // Time-based layout requires searching since the ingestion timestamp may vary
    return findBookRecursive(baseDir, bookId);
}

// Recursively scans the Datalake directory tree to find the files for a given book ID.
BookFiles Datalake::findBookRecursive(
    const fs::path& baseDir,
    int bookId
) {
    BookFiles files;
    if (!fs::exists(baseDir)) return files;

    std::string targetHeader = std::to_string(bookId) + ".header.txt";
    std::string targetBody = std::to_string(bookId) + ".body.txt";

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(baseDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (fname == targetHeader) {
            files.headerPath = entry.path();
        } else if (fname == targetBody) {
            files.bodyPath = entry.path();
        }

        if (!files.headerPath.empty() && !files.bodyPath.empty()) {
            files.exists = true;
            return files;
        }
    }

    files.exists = !files.headerPath.empty() && !files.bodyPath.empty();
    return files;
}

// Runs full benchmark comparison across all 3 datalake structures.
std::vector<DatalakeBenchmarkResult> Datalake::runBenchmark(
    const fs::path& benchRootDir,
    const std::map<int, std::pair<std::string, std::string>>& sampleContent,
    int numSyntheticBooks,
    int numLookups,
    const std::string& outputJsonPath
) {
    std::vector<DatalakeBenchmarkResult> results;
    if (sampleContent.empty()) {
        std::cerr << "[Datalake::runBenchmark] Error: No sample book content provided.\n";
        return results;
    }

    // Extract available sample IDs to cycle through
    std::vector<int> sampleIds;
    for (const auto& [id, _] : sampleContent) {
        sampleIds.push_back(id);
    }

    // Prepare synthetic book IDs (starting from 100000 to avoid conflicts)
    std::vector<int> syntheticIds;
    syntheticIds.reserve(numSyntheticBooks);
    for (int i = 0; i < numSyntheticBooks; ++i) {
        syntheticIds.push_back(100000 + i);
    }

    // Pick random subset for lookup benchmarking
    std::mt19937 rng(42); // Deterministic seed for reproducible comparisons
    std::vector<int> lookupSampleIds = syntheticIds;
    std::shuffle(lookupSampleIds.begin(), lookupSampleIds.end(), rng);
    if ((int)lookupSampleIds.size() > numLookups) {
        lookupSampleIds.resize(numLookups);
    }

    std::vector<DatalakeLayout> layouts = {
        DatalakeLayout::TimeBased,
        DatalakeLayout::BookBased,
        DatalakeLayout::BatchBased
    };

    for (DatalakeLayout layout : layouts) {
        std::string layoutName = layoutToString(layout);
        fs::path layoutDir = benchRootDir / layoutName;

        // Clean previous benchmark data if present
        std::error_code ec;
        fs::remove_all(layoutDir, ec);
        fs::create_directories(layoutDir, ec);

        DatalakeBenchmarkResult res;
        res.layoutName = layoutName;
        res.numBooks = numSyntheticBooks;

        // 1. Measure write throughput
        auto tStartWrite = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < numSyntheticBooks; ++i) {
            int bookId = syntheticIds[i];
            int sampleId = sampleIds[i % sampleIds.size()];
            const auto& [header, body] = sampleContent.at(sampleId);
            saveBook(layoutDir, layout, bookId, header, body);
        }
        auto tEndWrite = std::chrono::high_resolution_clock::now();
        res.writeSeconds = std::chrono::duration<double>(tEndWrite - tStartWrite).count();
        res.writeBooksPerSec = (res.writeSeconds > 0) ? (numSyntheticBooks / res.writeSeconds) : 0.0;

        // 2. Measure lookup latency
        res.lookupSamples = (int)lookupSampleIds.size();
        size_t totalBytesRead = 0;
        auto tStartLookup = std::chrono::high_resolution_clock::now();
        for (int bookId : lookupSampleIds) {
            BookFiles files = locateBook(layoutDir, layout, bookId);
            if (files.exists) {
                std::ifstream stream(files.bodyPath, std::ios::binary | std::ios::ate);
                if (stream.is_open()) {
                    totalBytesRead += stream.tellg();
                }
            }
        }
        (void)totalBytesRead;
        auto tEndLookup = std::chrono::high_resolution_clock::now();
        res.lookupSeconds = std::chrono::duration<double>(tEndLookup - tStartLookup).count();
        res.lookupAvgMs = (res.lookupSamples > 0) ? ((res.lookupSeconds / res.lookupSamples) * 1000.0) : 0.0;

        // 3. Measure filesystem storage overhead
        int numDirs = 0;
        int maxDepth = 0;
        std::map<fs::path, int> filesPerDir;

        for (const auto& entry : fs::recursive_directory_iterator(layoutDir, ec)) {
            if (entry.is_directory()) {
                numDirs++;
                // Compute depth relative to layoutDir
                auto rel = fs::relative(entry.path(), layoutDir);
                int depth = 0;
                for (const auto& part : rel) {
                    (void)part;
                    depth++;
                }
                if (depth > maxDepth) maxDepth = depth;
            } else if (entry.is_regular_file() && entry.path().extension() == ".txt") {
                filesPerDir[entry.path().parent_path()]++;
            }
        }

        res.numDirsCreated = numDirs;
        res.maxDepth = maxDepth;
        if (!filesPerDir.empty()) {
            double totalFiles = 0;
            int maxFiles = 0;
            for (const auto& [_, count] : filesPerDir) {
                totalFiles += count;
                if (count > maxFiles) maxFiles = count;
            }
            res.avgFilesPerDir = totalFiles / filesPerDir.size();
            res.maxFilesPerDir = maxFiles;
        }

        results.push_back(res);

        // Clean benchmark temporary data to conserve disk space
        fs::remove_all(layoutDir, ec);
    }

    // Export to JSON if path provided
    if (!outputJsonPath.empty()) {
        fs::path outPath(outputJsonPath);
        std::error_code ec;
        if (outPath.has_parent_path()) {
            fs::create_directories(outPath.parent_path(), ec);
        }
        std::ofstream jsonFile(outPath);
        if (jsonFile.is_open()) {
            jsonFile << "{\n";
            jsonFile << "  \"n_books\": " << numSyntheticBooks << ",\n";
            jsonFile << "  \"results\": [\n";
            for (size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                jsonFile << "    {\n";
                jsonFile << "      \"structure\": \"" << r.layoutName << "\",\n";
                jsonFile << "      \"n_books\": " << r.numBooks << ",\n";
                jsonFile << "      \"write_seconds\": " << std::fixed << std::setprecision(4) << r.writeSeconds << ",\n";
                jsonFile << "      \"write_books_per_sec\": " << std::fixed << std::setprecision(1) << r.writeBooksPerSec << ",\n";
                jsonFile << "      \"lookup_n\": " << r.lookupSamples << ",\n";
                jsonFile << "      \"lookup_seconds\": " << std::fixed << std::setprecision(4) << r.lookupSeconds << ",\n";
                jsonFile << "      \"lookup_avg_ms\": " << std::fixed << std::setprecision(4) << r.lookupAvgMs << ",\n";
                jsonFile << "      \"num_dirs_created\": " << r.numDirsCreated << ",\n";
                jsonFile << "      \"max_depth\": " << r.maxDepth << ",\n";
                jsonFile << "      \"avg_files_per_dir\": " << std::fixed << std::setprecision(1) << r.avgFilesPerDir << ",\n";
                jsonFile << "      \"max_files_per_dir\": " << r.maxFilesPerDir << "\n";
                jsonFile << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
            }
            jsonFile << "  ]\n";
            jsonFile << "}\n";
        }
    }

    return results;
}
