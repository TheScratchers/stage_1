#pragma once

#include <string>
#include <vector>
#include <optional>

// Represents metadata fields extracted from a Gutenberg book header.
struct BookMetadata {
    int bookId = 0;
    std::string title = "Unknown";
    std::string author = "Unknown";
    std::string language = "Unknown";
    std::string headerPath = "";
    std::string bodyPath = "";
    std::string ingestedAt = "";
};

// Statistical result of a metadata database query/insertion benchmark.
struct MetadataBenchmarkResult {
    int totalBooks = 0;
    double insertTotalSeconds = 0.0;
    double insertBooksPerSec = 0.0;
    int numAuthorQueries = 0;
    double queryAuthorAvgMs = 0.0;
    int numTitleQueries = 0;
    double queryTitleAvgMs = 0.0;
};

// Extracts and stores metadata for books in the datamart using SQLite
// and CSV formats, providing query operations by author, title, and book ID.
class MetadataExtractor {
public:
    // Parses title, author, and language from a Project Gutenberg header string.
    static BookMetadata parseHeader(int bookId, const std::string& headerText);

    // Initializes the SQLite database schema if it does not already exist.
    static bool initDatabase(const std::string& dbPath);

    // Inserts or replaces a book record in the SQLite database.
    static bool saveToDatabase(const BookMetadata& metadata, const std::string& dbPath);

    // Appends the book metadata to a CSV file in the datamart directory.
    static bool saveToCsv(const BookMetadata& metadata, const std::string& csvPath);

    // Queries a book by its unique ID.
    static std::optional<BookMetadata> queryById(int bookId, const std::string& dbPath);

    // Queries books matching an author name substring (case-insensitive).
    static std::vector<BookMetadata> queryByAuthor(const std::string& authorSubstring, const std::string& dbPath);

    // Queries books matching a title substring (case-insensitive).
    static std::vector<BookMetadata> queryByTitle(const std::string& titleSubstring, const std::string& dbPath);

    // Retrieves all books currently stored in the database.
    static std::vector<BookMetadata> getAllBooks(const std::string& dbPath);

    // Runs insertion and query performance benchmarks on the SQLite database.
    static MetadataBenchmarkResult runBenchmark(
        const std::string& dbPath,
        const std::vector<BookMetadata>& records,
        int numQueries = 50
    );
};
