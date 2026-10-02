#pragma once

#include <string>
#include <vector>
#include <optional>

struct BookMetadata {
    int bookId = 0;
    std::string title = "Unknown";
    std::string author = "Unknown";
    std::string language = "Unknown";
    std::string headerPath = "";
    std::string bodyPath = "";
    std::string ingestedAt = "";
};

// Extracts metadata via regex and persists records in SQLite.
class MetadataExtractor {
public:
    static BookMetadata parseHeader(int bookId, const std::string& headerText);
    static bool initDatabase(const std::string& dbPath);
    static bool saveToDatabase(const BookMetadata& metadata, const std::string& dbPath);
    static bool saveBatchToDatabase(const std::vector<BookMetadata>& list, const std::string& dbPath);
    static std::optional<BookMetadata> queryById(int bookId, const std::string& dbPath);
    static std::vector<BookMetadata> queryByAuthor(const std::string& author, const std::string& dbPath);
    static std::vector<BookMetadata> queryByTitle(const std::string& title, const std::string& dbPath);
    static std::vector<BookMetadata> getAllBooks(const std::string& dbPath);
};
