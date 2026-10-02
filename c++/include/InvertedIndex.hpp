#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <chrono>
#include <filesystem>

// Identifies the inverted index storage structure.
enum class IndexStructureType {
    MonolithicJson,    // datamarts/inverted_index.json
    HierarchicalFiles, // datamarts/inverted_index/<Letter>/<term>.txt
    BinaryCompact      // datamarts/inverted_index.bin (custom high-performance binary format)
};

// Represents the benchmark metrics for an inverted index implementation.
struct IndexBenchmarkResult {
    std::string structureName;
    int numTerms = 0;
    int numBooks = 0;
    double buildSeconds = 0.0;
    double coldLoadSeconds = 0.0;
    double avgLookupMs = 0.0;
    int numFiles = 0;
    int numDirs = 0;
    uintmax_t totalSizeBytes = 0;
};

// Builds, updates, searches, and benchmarks the inverted index across
// 3 distinct storage architectures:
//   1. Single Monolithic File (JSON format)
//   2. Hierarchical File System (folder per letter, file per term)
//   3. Custom Binary Compact Index (dictionary table + contiguous postings with direct seek)
class InvertedIndex {
public:
    // Tokenizes raw text into lowercased alphabetic tokens, stripping punctuation.
    static std::set<std::string> tokenize(const std::string& text);

    // =========================================================================
    // Structure 1: Monolithic JSON (inverted_index.json)
    // =========================================================================
    static bool updateMonolithicIndex(int bookId, const std::string& bodyText, const std::string& jsonPath);
    static std::map<std::string, std::vector<int>> loadMonolithicIndex(const std::string& jsonPath);
    static bool saveMonolithicIndex(const std::map<std::string, std::vector<int>>& index, const std::string& jsonPath);
    static std::vector<int> searchMonolithic(const std::string& term, const std::string& jsonPath);

    // =========================================================================
    // Structure 2: Hierarchical Folders (inverted_index/<Letter>/<term>.txt)
    // =========================================================================
    static bool updateHierarchicalIndex(int bookId, const std::string& bodyText, const std::string& rootDir);
    static std::vector<int> searchHierarchical(const std::string& term, const std::string& rootDir);

    // =========================================================================
    // Structure 3: Custom Binary Compact Index (inverted_index.bin)
    // =========================================================================
    static bool updateBinaryIndex(int bookId, const std::string& bodyText, const std::string& binPath);
    static bool buildBinaryIndexFromMap(const std::map<std::string, std::vector<int>>& indexMap, const std::string& binPath);
    static std::vector<int> searchBinary(const std::string& term, const std::string& binPath);

    // =========================================================================
    // Unified Query Engine (Single term, Boolean AND, Boolean OR)
    // =========================================================================
    static std::vector<int> querySingle(const std::string& term, IndexStructureType type, const std::string& basePath);
    static std::vector<int> queryAnd(const std::vector<std::string>& terms, IndexStructureType type, const std::string& basePath);
    static std::vector<int> queryOr(const std::vector<std::string>& terms, IndexStructureType type, const std::string& basePath);

    // =========================================================================
    // Benchmarking Suite
    // =========================================================================
    static std::vector<IndexBenchmarkResult> runBenchmark(
        const std::filesystem::path& benchDir,
        const std::map<int, std::string>& bookBodies,
        const std::vector<std::string>& queryTerms,
        const std::string& outputJsonPath = ""
    );

    // Helper: converts structure enum to string
    static std::string structureToString(IndexStructureType type);
};
