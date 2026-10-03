#include "SqliteIndex.hpp"
#include "Tokenizer.hpp"
#include <sqlite3.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

bool SqliteIndex::initDatabase(const std::string& dbPath) {
    std::error_code ec;
    fs::create_directories(fs::path(dbPath).parent_path(), ec);

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }

    const char* ddl =
        "CREATE TABLE IF NOT EXISTS inverted_index ("
        "term TEXT NOT NULL, "
        "book_id INTEGER NOT NULL, "
        "PRIMARY KEY (term, book_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_term ON inverted_index(term);";

    char* err = nullptr;
    int rc = sqlite3_exec(db, ddl, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err) sqlite3_free(err);
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);
    return true;
}

bool SqliteIndex::buildFromMap(const std::map<std::string, std::vector<int>>& indexMap, const std::string& dbPath) {
    std::error_code ec;
    fs::remove(dbPath, ec);
    if (!initDatabase(dbPath)) return false;

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_exec(db, "PRAGMA synchronous = OFF;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA journal_mode = MEMORY;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* insertSql = "INSERT OR IGNORE INTO inverted_index (term, book_id) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    for (const auto& [term, ids] : indexMap) {
        for (int id : ids) {
            sqlite3_bind_text(stmt, 1, term.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 2, id);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return true;
}

std::vector<int> SqliteIndex::search(const std::string& term, const std::string& dbPath) {
    std::vector<int> results;
    if (!fs::exists(dbPath)) return results;

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return results;
    }

    const char* sql = "SELECT book_id FROM inverted_index WHERE term = ? ORDER BY book_id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return results;
    }

    std::string lowerTerm = Tokenizer::toLower(term);
    sqlite3_bind_text(stmt, 1, lowerTerm.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(sqlite3_column_int(stmt, 0));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return results;
}

bool SqliteIndex::update(int bookId, const std::string& text, const std::string& dbPath) {
    if (!fs::exists(dbPath)) {
        initDatabase(dbPath);
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* insertSql = "INSERT OR IGNORE INTO inverted_index (term, book_id) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    for (const auto& term : Tokenizer::extractUniqueTokens(text)) {
        sqlite3_bind_text(stmt, 1, term.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, bookId);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return true;
}
