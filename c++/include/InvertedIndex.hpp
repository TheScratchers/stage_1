#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>

// Builds and maintains the inverted index datamart in both monolithic and hierarchical formats.
class InvertedIndex {
public:
    // Tokenizes raw text into a set of unique lowercased words.
    static std::set<std::string> tokenize(const std::string& text);

    // Updates the monolithic JSON inverted index file with terms from a book.
    static bool updateMonolithicIndex(int bookId, const std::string& bodyText, const std::string& jsonPath);

    // Updates the hierarchical folder inverted index with terms from a book.
    static bool updateHierarchicalIndex(int bookId, const std::string& bodyText, const std::string& rootDir);
};
