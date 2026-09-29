#include "MetadataExtractor.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <chrono>
#include <sqlite3.h>

namespace fs = std::filesystem;

// Parses title, author, and language from a Project Gutenberg header string.
BookMetadata MetadataExtractor::parseHeader(int bookId, const std::string& headerText) {
    BookMetadata meta;
    meta.bookId = bookId;
    meta.title = "Unknown";
    meta.author = "Unknown";
    meta.language = "Unknown";

    std::regex titleRegex(R"(^Title:\s*(.+)$)", std::regex::icase);
    std::regex authorRegex(R"(^Author:\s*(.+)$)", std::regex::icase);
    std::regex langRegex(R"(^Language:\s*(.+)$)", std::regex::icase);

    std::istringstream stream(headerText);
    std::string line;
    std::smatch match;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (meta.title == "Unknown" && std::regex_match(line, match, titleRegex)) {
            meta.title = match[1].str();
        } else if (meta.author == "Unknown" && std::regex_match(line, match, authorRegex)) {
            meta.author = match[1].str();
        } else if (meta.language == "Unknown" && std::regex_match(line, match, langRegex)) {
            meta.language = match[1].str();
        }
    }

    return meta;
}

// Initializes the SQLite database schema if not already created.
bool MetadataExtractor::initDatabase(const std::string& dbPath) {
    fs::path p(dbPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[MetadataExtractor::initDatabase] Cannot open SQLite DB: " << sqlite3_errmsg(db) << "\n";
        if (db) sqlite3_close(db);
        return false;
    }

    const char* createSql = 
        "CREATE TABLE IF NOT EXISTS books ("
        "  book_id INTEGER PRIMARY KEY,"
        "  title TEXT,"
        "  author TEXT,"
        "  language TEXT,"
        "  header_path TEXT,"
        "  body_path TEXT,"
        "  ingested_at TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_books_author ON books(author);"
        "CREATE INDEX IF NOT EXISTS idx_books_title ON books(title);";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, createSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[MetadataExtractor::initDatabase] Table creation error: " << (errMsg ? errMsg : "unknown") << "\n";
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

// Inserts or replaces a book record in SQLite using parameterized prepared statements.
bool MetadataExtractor::saveToDatabase(const BookMetadata& metadata, const std::string& dbPath) {
    if (!initDatabase(dbPath)) return false;

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[MetadataExtractor::saveToDatabase] DB Open Error: " << sqlite3_errmsg(db) << "\n";
        if (db) sqlite3_close(db);
        return false;
    }

    const char* insertSql = 
        "INSERT OR REPLACE INTO books (book_id, title, author, language, header_path, body_path, ingested_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[MetadataExtractor::saveToDatabase] Prepare Error: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_int(stmt, 1, metadata.bookId);
    sqlite3_bind_text(stmt, 2, metadata.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, metadata.author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, metadata.language.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, metadata.headerPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, metadata.bodyPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, metadata.ingestedAt.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return (rc == SQLITE_DONE);
}

// Appends metadata to CSV file.
bool MetadataExtractor::saveToCsv(const BookMetadata& metadata, const std::string& csvPath) {
    fs::path path(csvPath);
    if (path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    }

    bool fileExists = fs::exists(path);
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) {
        return false;
    }

    if (!fileExists) {
        out << "book_id,title,author,language\n";
    }

    auto escapeCsv = [](const std::string& field) {
        std::string res = "\"";
        for (char c : field) {
            if (c == '"') res += "\"\"";
            else res += c;
        }
        res += "\"";
        return res;
    };

    out << metadata.bookId << ","
        << escapeCsv(metadata.title) << ","
        << escapeCsv(metadata.author) << ","
        << escapeCsv(metadata.language) << "\n";

    return true;
}

// Helper function to extract a BookMetadata object from a loaded SQLite statement.
static BookMetadata rowToMetadata(sqlite3_stmt* stmt) {
    BookMetadata m;
    m.bookId = sqlite3_column_int(stmt, 0);
    const unsigned char* t = sqlite3_column_text(stmt, 1);
    const unsigned char* a = sqlite3_column_text(stmt, 2);
    const unsigned char* l = sqlite3_column_text(stmt, 3);
    const unsigned char* hp = sqlite3_column_text(stmt, 4);
    const unsigned char* bp = sqlite3_column_text(stmt, 5);
    const unsigned char* ia = sqlite3_column_text(stmt, 6);

    if (t) m.title = reinterpret_cast<const char*>(t);
    if (a) m.author = reinterpret_cast<const char*>(a);
    if (l) m.language = reinterpret_cast<const char*>(l);
    if (hp) m.headerPath = reinterpret_cast<const char*>(hp);
    if (bp) m.bodyPath = reinterpret_cast<const char*>(bp);
    if (ia) m.ingestedAt = reinterpret_cast<const char*>(ia);

    return m;
}

// Queries a book by ID.
std::optional<BookMetadata> MetadataExtractor::queryById(int bookId, const std::string& dbPath) {
    if (!fs::exists(dbPath)) return std::nullopt;

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return std::nullopt;
    }

    const char* sql = "SELECT book_id, title, author, language, header_path, body_path, ingested_at FROM books WHERE book_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, bookId);

    std::optional<BookMetadata> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToMetadata(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

// Queries books matching an author substring.
std::vector<BookMetadata> MetadataExtractor::queryByAuthor(const std::string& authorSubstring, const std::string& dbPath) {
    std::vector<BookMetadata> results;
    if (!fs::exists(dbPath)) return results;

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return results;
    }

    const char* sql = "SELECT book_id, title, author, language, header_path, body_path, ingested_at FROM books WHERE author LIKE ? ORDER BY book_id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return results;
    }

    std::string pattern = "%" + authorSubstring + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(rowToMetadata(stmt));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return results;
}

// Queries books matching a title substring.
std::vector<BookMetadata> MetadataExtractor::queryByTitle(const std::string& titleSubstring, const std::string& dbPath) {
    std::vector<BookMetadata> results;
    if (!fs::exists(dbPath)) return results;

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return results;
    }

    const char* sql = "SELECT book_id, title, author, language, header_path, body_path, ingested_at FROM books WHERE title LIKE ? ORDER BY book_id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return results;
    }

    std::string pattern = "%" + titleSubstring + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(rowToMetadata(stmt));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return results;
}

// Retrieves all books from the database.
std::vector<BookMetadata> MetadataExtractor::getAllBooks(const std::string& dbPath) {
    std::vector<BookMetadata> results;
    if (!fs::exists(dbPath)) return results;

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return results;
    }

    const char* sql = "SELECT book_id, title, author, language, header_path, body_path, ingested_at FROM books ORDER BY book_id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(rowToMetadata(stmt));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return results;
}

// Runs insertion and query performance benchmarks on SQLite.
MetadataBenchmarkResult MetadataExtractor::runBenchmark(
    const std::string& dbPath,
    const std::vector<BookMetadata>& records,
    int numQueries
) {
    MetadataBenchmarkResult result;
    result.totalBooks = (int)records.size();
    if (records.empty()) return result;

    // Reset benchmark db
    std::error_code ec;
    fs::remove(dbPath, ec);
    initDatabase(dbPath);

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return result;
    }

    // 1. Measure insert speed using transaction
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* insertSql = "INSERT INTO books (book_id, title, author, language, header_path, body_path, ingested_at) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr);

    auto tStartInsert = std::chrono::high_resolution_clock::now();
    for (const auto& meta : records) {
        sqlite3_bind_int(stmt, 1, meta.bookId);
        sqlite3_bind_text(stmt, 2, meta.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, meta.author.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, meta.language.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, meta.headerPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, meta.bodyPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, meta.ingestedAt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    auto tEndInsert = std::chrono::high_resolution_clock::now();
    sqlite3_finalize(stmt);

    result.insertTotalSeconds = std::chrono::duration<double>(tEndInsert - tStartInsert).count();
    result.insertBooksPerSec = (result.insertTotalSeconds > 0) ? (result.totalBooks / result.insertTotalSeconds) : 0.0;

    // 2. Measure query performance
    result.numAuthorQueries = numQueries;
    auto tStartAuthor = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numQueries; ++i) {
        const auto& targetAuthor = records[i % records.size()].author;
        queryByAuthor(targetAuthor, dbPath);
    }
    auto tEndAuthor = std::chrono::high_resolution_clock::now();
    double authorSecs = std::chrono::duration<double>(tEndAuthor - tStartAuthor).count();
    result.queryAuthorAvgMs = (numQueries > 0) ? ((authorSecs / numQueries) * 1000.0) : 0.0;

    result.numTitleQueries = numQueries;
    auto tStartTitle = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numQueries; ++i) {
        const auto& targetTitle = records[i % records.size()].title;
        queryByTitle(targetTitle, dbPath);
    }
    auto tEndTitle = std::chrono::high_resolution_clock::now();
    double titleSecs = std::chrono::duration<double>(tEndTitle - tStartTitle).count();
    result.queryTitleAvgMs = (numQueries > 0) ? ((titleSecs / numQueries) * 1000.0) : 0.0;

    sqlite3_close(db);
    return result;
}
