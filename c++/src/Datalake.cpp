#include "Datalake.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

std::string Datalake::layoutToString(DatalakeLayout layout) {
    switch (layout) {
        case DatalakeLayout::TimeBased:  return "time_based";
        case DatalakeLayout::BookBased:  return "book_based";
        case DatalakeLayout::BatchBased: return "batch_based";
    }
    return "unknown";
}

fs::path Datalake::getDirectoryPath(
    const fs::path& baseDir,
    DatalakeLayout layout,
    int bookId,
    std::chrono::system_clock::time_point timestamp
) {
    if (layout == DatalakeLayout::TimeBased) {
        std::time_t t = std::chrono::system_clock::to_time_t(timestamp);
        std::tm tm{};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d/%H");
        return baseDir / oss.str();
    }
    if (layout == DatalakeLayout::BookBased) {
        return baseDir / std::to_string(bookId);
    }
    return baseDir / ("batch_" + std::to_string(bookId / 500));
}

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

    std::ofstream h(targetDir / (std::to_string(bookId) + ".header.txt"), std::ios::binary);
    std::ofstream b(targetDir / (std::to_string(bookId) + ".body.txt"), std::ios::binary);
    if (!h.is_open() || !b.is_open()) return false;

    h.write(headerContent.data(), headerContent.size());
    b.write(bodyContent.data(), bodyContent.size());
    return true;
}

BookFiles Datalake::locateBook(const fs::path& baseDir, DatalakeLayout layout, int bookId) {
    if (layout == DatalakeLayout::BookBased || layout == DatalakeLayout::BatchBased) {
        fs::path dir = getDirectoryPath(baseDir, layout, bookId);
        BookFiles f{dir / (std::to_string(bookId) + ".header.txt"),
                    dir / (std::to_string(bookId) + ".body.txt"), false};
        f.exists = fs::exists(f.headerPath) && fs::exists(f.bodyPath);
        if (f.exists) return f;
    }
    return findBookRecursive(baseDir, bookId);
}

BookFiles Datalake::findBookRecursive(const fs::path& baseDir, int bookId) {
    BookFiles files;
    if (!fs::exists(baseDir)) return files;

    std::string hName = std::to_string(bookId) + ".header.txt";
    std::string bName = std::to_string(bookId) + ".body.txt";
    std::error_code ec;

    for (const auto& entry : fs::recursive_directory_iterator(baseDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (fname == hName) files.headerPath = entry.path();
        else if (fname == bName) files.bodyPath = entry.path();

        if (!files.headerPath.empty() && !files.bodyPath.empty()) {
            files.exists = true;
            return files;
        }
    }
    files.exists = !files.headerPath.empty() && !files.bodyPath.empty();
    return files;
}
