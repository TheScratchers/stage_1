#pragma once

#include <string>
#include <filesystem>
#include <chrono>

enum class DatalakeLayout { TimeBased, BookBased, BatchBased };

struct BookFiles {
    std::filesystem::path headerPath;
    std::filesystem::path bodyPath;
    bool exists = false;
};

// Manages raw book storage across TimeBased, BookBased, and BatchBased layouts.
class Datalake {
public:
    static std::filesystem::path getDirectoryPath(
        const std::filesystem::path& baseDir,
        DatalakeLayout layout,
        int bookId,
        std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now()
    );

    static bool saveBook(
        const std::filesystem::path& baseDir,
        DatalakeLayout layout,
        int bookId,
        const std::string& headerContent,
        const std::string& bodyContent,
        std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now()
    );

    static BookFiles locateBook(
        const std::filesystem::path& baseDir,
        DatalakeLayout layout,
        int bookId
    );

    static BookFiles findBookRecursive(
        const std::filesystem::path& baseDir,
        int bookId
    );

    static std::string layoutToString(DatalakeLayout layout);

    static std::vector<int> detectNewBooks(
        const std::filesystem::path& baseDir,
        DatalakeLayout layout,
        const std::vector<int>& candidateIds
    );

    static int recoverDatalake(
        const std::filesystem::path& baseDir,
        DatalakeLayout layout,
        const std::vector<int>& expectedIds,
        const std::map<int, std::pair<std::string, std::string>>& fallbackData
    );
};
