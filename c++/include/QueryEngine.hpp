#pragma once

#include <string>
#include <vector>

enum class IndexType { Json, Hierarchical, Binary };

// Executes single-term and boolean searches across any of the 3 inverted index structures.
class QueryEngine {
public:
    static std::vector<int> searchSingle(const std::string& term, IndexType type, const std::string& basePath);
    static std::vector<int> searchAnd(const std::vector<std::string>& terms, IndexType type, const std::string& basePath);
    static std::vector<int> searchOr(const std::vector<std::string>& terms, IndexType type, const std::string& basePath);
};
