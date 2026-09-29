#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <filesystem>

// Defines the storage organization strategies for the Datalake
// as required by Section 3.1 of the Stage 1 specification.
enum class DatalakeLayout {
    TimeBased,   // YYYYMMDD/HH/<BOOK_ID>.{header,body}.txt
    BookBased,   // <BOOK_ID>/<BOOK_ID>.{header,body}.txt
    BatchBased   // batch_<BATCH_NUM>/<BOOK_ID>.{header,body}.txt (e.g. 500 books per folder)
};

// Represents a pair of paths to a book's files on disk.
struct BookFiles {
    std::filesystem::path headerPath;
    std::filesystem::path bodyPath;
    bool exists = false;
};

// Represents statistical results from a Datalake layout benchmark experiment.
struct DatalakeBenchmarkResult {
    std::string layoutName;
    int numBooks = 0;
    double writeSeconds = 0.0;
    double writeBooksPerSec = 0.0;
    int lookupSamples = 0;
    double lookupSeconds = 0.0;
    double lookupAvgMs = 0.0;
    int numDirsCreated = 0;
    int maxDepth = 0;
    double avgFilesPerDir = 0.0;
    int maxFilesPerDir = 0;
};

// Manages book storage, layout mapping, path resolution, and benchmarking across
// different Datalake hierarchical structures.
class Datalake {
public:
    // Computes the directory path where a book should be saved for a given layout.
    static std::filesystem::path getDirectoryPath(
        const std::filesystem::path& baseDir,
        DatalakeLayout layout,
        int bookId,
        std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now()
    );

    // Saves the split header and body files into the designated layout directory.
    static bool saveBook(
        const std::filesystem::path& baseDir,
        DatalakeLayout layout,
        int bookId,
        const std::string& headerContent,
        const std::string& bodyContent,
        std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now()
    );

    // Locates a book's header and body files on disk given a layout and base directory.
    static BookFiles locateBook(
        const std::filesystem::path& baseDir,
        DatalakeLayout layout,
        int bookId
    );

    // Recursively scans the Datalake to find any book regardless of layout partition.
    static BookFiles findBookRecursive(
        const std::filesystem::path& baseDir,
        int bookId
    );

    // Runs full benchmark comparison across all 3 datalake structures
    // measuring write throughput, lookup latency, and filesystem overhead.
    static std::vector<DatalakeBenchmarkResult> runBenchmark(
        const std::filesystem::path& benchRootDir,
        const std::map<int, std::pair<std::string, std::string>>& sampleContent,
        int numSyntheticBooks = 500,
        int numLookups = 100,
        const std::string& outputJsonPath = ""
    );

    // Converts layout enum to human-readable string identifier.
    static std::string layoutToString(DatalakeLayout layout);
};
