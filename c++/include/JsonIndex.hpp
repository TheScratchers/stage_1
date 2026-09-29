#pragma once

#include <string>
#include <vector>
#include <map>

// Manages the monolithic JSON inverted index using nlohmann::json.
class JsonIndex {
public:
    static std::map<std::string, std::vector<int>> load(const std::string& jsonPath);
    static bool save(const std::map<std::string, std::vector<int>>& index, const std::string& jsonPath);
    static bool update(int bookId, const std::string& bodyText, const std::string& jsonPath);
    static std::vector<int> search(const std::string& term, const std::string& jsonPath);
};
