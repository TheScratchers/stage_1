#include "MetadataExtractor.hpp"
#include <sstream>
#include <regex>
#include <filesystem>
#include <sqlite3.h>

namespace fs = std::filesystem;

BookMetadata MetadataExtractor::parseHeader(int bookId, const std::string& headerText) {
    BookMetadata meta;
    meta.bookId = bookId;
    std::regex regTitle(R"(^Title:\s*(.+)$)", std::regex::icase);
    std::regex regAuthor(R"(^Author:\s*(.+)$)", std::regex::icase);
    std::regex regLang(R"(^Language:\s*(.+)$)", std::regex::icase);

    std::istringstream stream(headerText);
    std::string line;
    std::smatch m;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (meta.title == "Unknown" && std::regex_match(line, m, regTitle)) meta.title = m[1].str();
        else if (meta.author == "Unknown" && std::regex_match(line, m, regAuthor)) meta.author = m[1].str();
        else if (meta.language == "Unknown" && std::regex_match(line, m, regLang)) meta.language = m[1].str();
    }
    return meta;
}

bool MetadataExtractor::initDatabase(const std::string& dbPath) {
    fs::path p(dbPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }
    const char* sql =
        "CREATE TABLE IF NOT EXISTS books ("
        "  book_id INTEGER PRIMARY KEY, title TEXT, author TEXT, language TEXT,"
        "  header_path TEXT, body_path TEXT, ingested_at TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_author ON books(author);"
        "CREATE INDEX IF NOT EXISTS idx_title ON books(title);";
    char* err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    sqlite3_close(db);
    return true;
}

bool MetadataExtractor::saveBatchToDatabase(const std::vector<BookMetadata>& list, const std::string& dbPath) {
    if (!initDatabase(dbPath) || list.empty()) return false;
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) return false;

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sql = "INSERT OR REPLACE INTO books VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    for (const auto& m : list) {
        sqlite3_bind_int(stmt, 1, m.bookId);
        sqlite3_bind_text(stmt, 2, m.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, m.author.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, m.language.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, m.headerPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, m.bodyPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, m.ingestedAt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return true;
}

bool MetadataExtractor::saveToDatabase(const BookMetadata& m, const std::string& dbPath) {
    return saveBatchToDatabase({m}, dbPath);
}

static BookMetadata readRow(sqlite3_stmt* stmt) {
    auto col = [&](int i) -> std::string {
        const unsigned char* c = sqlite3_column_text(stmt, i);
        return c ? reinterpret_cast<const char*>(c) : "";
    };
    return {sqlite3_column_int(stmt, 0), col(1), col(2), col(3), col(4), col(5), col(6)};
}

static std::vector<BookMetadata> selectBooks(const std::string& dbPath, const std::string& sql, const std::string* filter = nullptr) {
    std::vector<BookMetadata> out;
    if (!fs::exists(dbPath)) return out;
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) return out;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (filter) {
            std::string pat = "%" + *filter + "%";
            sqlite3_bind_text(stmt, 1, pat.c_str(), -1, SQLITE_TRANSIENT);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(readRow(stmt));
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return out;
}

std::optional<BookMetadata> MetadataExtractor::queryById(int bookId, const std::string& dbPath) {
    auto res = selectBooks(dbPath, "SELECT * FROM books WHERE book_id = " + std::to_string(bookId) + ";");
    return res.empty() ? std::nullopt : std::make_optional(res.front());
}

std::vector<BookMetadata> MetadataExtractor::queryByAuthor(const std::string& a, const std::string& db) {
    return selectBooks(db, "SELECT * FROM books WHERE author LIKE ? ORDER BY book_id ASC;", &a);
}

std::vector<BookMetadata> MetadataExtractor::queryByTitle(const std::string& t, const std::string& db) {
    return selectBooks(db, "SELECT * FROM books WHERE title LIKE ? ORDER BY book_id ASC;", &t);
}

std::vector<BookMetadata> MetadataExtractor::getAllBooks(const std::string& db) {
    return selectBooks(db, "SELECT * FROM books ORDER BY book_id ASC;");
}
