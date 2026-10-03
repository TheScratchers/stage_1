#pragma once

#include <string>
#include <vector>
#include <map>

// Relational SQLite Inverted Index datamart (term, book_id)
class SqliteIndex {
public:
    static bool initDatabase(const std::string& dbPath);
    static bool buildFromMap(const std::map<std::string, std::vector<int>>& indexMap, const std::string& dbPath);
    static std::vector<int> search(const std::string& term, const std::string& dbPath);
    static bool update(int bookId, const std::string& text, const std::string& dbPath);
};
