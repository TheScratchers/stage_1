#pragma once

#include <string>
#include <vector>
#include <map>

// High-performance compact binary index with magic header "BIDX" and direct random file seek.
class BinaryIndex {
public:
    static bool buildFromMap(const std::map<std::string, std::vector<int>>& indexMap, const std::string& binPath);
    static std::map<std::string, std::vector<int>> load(const std::string& binPath);
    static bool update(int bookId, const std::string& bodyText, const std::string& binPath);
    static std::vector<int> search(const std::string& term, const std::string& binPath);
};
