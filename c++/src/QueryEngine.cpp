#include "QueryEngine.hpp"
#include "JsonIndex.hpp"
#include "HierarchicalIndex.hpp"
#include "BinaryIndex.hpp"
#include <algorithm>
#include <set>

std::vector<int> QueryEngine::searchSingle(const std::string& term, IndexType type, const std::string& basePath) {
    switch (type) {
        case IndexType::Json:         return JsonIndex::search(term, basePath);
        case IndexType::Hierarchical: return HierarchicalIndex::search(term, basePath);
        case IndexType::Binary:       return BinaryIndex::search(term, basePath);
    }
    return {};
}

std::vector<int> QueryEngine::searchAnd(const std::vector<std::string>& terms, IndexType type, const std::string& basePath) {
    if (terms.empty()) return {};

    std::vector<int> result = searchSingle(terms[0], type, basePath);
    for (size_t i = 1; i < terms.size() && !result.empty(); ++i) {
        std::vector<int> next = searchSingle(terms[i], type, basePath);
        std::vector<int> intersection;
        std::set_intersection(
            result.begin(), result.end(),
            next.begin(), next.end(),
            std::back_inserter(intersection)
        );
        result = std::move(intersection);
    }
    return result;
}

std::vector<int> QueryEngine::searchOr(const std::vector<std::string>& terms, IndexType type, const std::string& basePath) {
    std::set<int> unionSet;
    for (const auto& term : terms) {
        for (int id : searchSingle(term, type, basePath)) {
            unionSet.insert(id);
        }
    }
    return std::vector<int>(unionSet.begin(), unionSet.end());
}
