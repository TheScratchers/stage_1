#include "JsonIndex.hpp"
#include "Tokenizer.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::map<std::string, std::vector<int>> JsonIndex::load(const std::string& jsonPath) {
    std::map<std::string, std::vector<int>> index;
    if (!fs::exists(jsonPath)) return index;

    std::ifstream file(jsonPath);
    if (!file.is_open()) return index;

    try {
        json j;
        file >> j;
        index = j.get<std::map<std::string, std::vector<int>>>();
    } catch (...) {}

    return index;
}

bool JsonIndex::save(const std::map<std::string, std::vector<int>>& index, const std::string& jsonPath) {
    fs::path p(jsonPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    std::ofstream file(jsonPath);
    if (!file.is_open()) return false;

    json j = index;
    file << j.dump(2) << "\n";
    return true;
}

bool JsonIndex::update(int bookId, const std::string& bodyText, const std::string& jsonPath) {
    auto index = load(jsonPath);
    for (const auto& term : Tokenizer::extractUniqueTokens(bodyText)) {
        auto& ids = index[term];
        if (std::find(ids.begin(), ids.end(), bookId) == ids.end()) {
            ids.push_back(bookId);
            std::sort(ids.begin(), ids.end());
        }
    }
    return save(index, jsonPath);
}

std::vector<int> JsonIndex::search(const std::string& term, const std::string& jsonPath) {
    auto index = load(jsonPath);
    std::string lower = Tokenizer::toLower(term);
    auto it = index.find(lower);
    return (it != index.end()) ? it->second : std::vector<int>{};
}
