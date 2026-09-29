#pragma once

#include <string>
#include <vector>

// Manages the hierarchical inverted index organized as directories by initial letter.
class HierarchicalIndex {
public:
    static bool update(int bookId, const std::string& bodyText, const std::string& rootDir);
    static std::vector<int> search(const std::string& term, const std::string& rootDir);
};
