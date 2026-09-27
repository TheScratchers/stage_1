#pragma once

#include <string>

// Represents metadata fields extracted from a Gutenberg book header.
struct BookMetadata {
    int bookId;
    std::string title;
    std::string author;
    std::string language;
};

// Extracts and stores metadata for books in the datamart.
class MetadataExtractor {
public:
    // Parses title, author, and language from a Project Gutenberg header string.
    static BookMetadata parseHeader(int bookId, const std::string& headerText);

    // Appends the book metadata to a CSV file in the datamart directory.
    static bool saveToCsv(const BookMetadata& metadata, const std::string& csvPath);
};
