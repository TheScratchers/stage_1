#include "InvertedIndex.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <regex>

namespace fs = std::filesystem;

// Tokenizes raw text into a set of unique lowercased words.
std::set<std::string> InvertedIndex::tokenize(const std::string& text) {
    std::set<std::string> tokens;
    std::string current;

    for (unsigned char ch : text) {
        if (std::isalpha(ch)) {
            current += static_cast<char>(std::tolower(ch));
        } else if (!current.empty()) {
            tokens.insert(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.insert(current);
    }
    return tokens;
}

// Updates the monolithic JSON inverted index file with terms from a book.
bool InvertedIndex::updateMonolithicIndex(int bookId, const std::string& bodyText, const std::string& jsonPath) {
    fs::path path(jsonPath);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }

    std::map<std::string, std::set<int>> index;

    // Load existing index if the file exists
    if (fs::exists(path)) {
        std::ifstream in(path);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        std::regex entryRegex(R"raw("([^"]+)":\s*\[([^\]]*)\])raw");
        auto itBegin = std::sregex_iterator(content.begin(), content.end(), entryRegex);
        auto itEnd = std::sregex_iterator();

        for (auto it = itBegin; it != itEnd; ++it) {
            std::string term = (*it)[1].str();
            std::string idsStr = (*it)[2].str();

            std::istringstream stream(idsStr);
            std::string token;
            while (std::getline(stream, token, ',')) {
                try {
                    index[term].insert(std::stoi(token));
                } catch (...) {
                }
            }
        }
    }

    // Add unique tokens from the current book
    std::set<std::string> terms = tokenize(bodyText);
    for (const auto& term : terms) {
        index[term].insert(bookId);
    }

    // Write back updated index to JSON file
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    out << "{\n";
    bool firstTerm = true;
    for (const auto& [term, ids] : index) {
        if (!firstTerm) {
            out << ",\n";
        }
        firstTerm = false;

        out << "  \"" << term << "\": [";
        bool firstId = true;
        for (int id : ids) {
            if (!firstId) out << ", ";
            firstId = false;
            out << id;
        }
        out << "]";
    }
    out << "\n}\n";

    return true;
}

// Updates the hierarchical folder inverted index with terms from a book.
bool InvertedIndex::updateHierarchicalIndex(int bookId, const std::string& bodyText, const std::string& rootDir) {
    std::set<std::string> terms = tokenize(bodyText);

    for (const auto& term : terms) {
        char firstChar = std::toupper(static_cast<unsigned char>(term[0]));
        std::string subDir = (firstChar >= 'A' && firstChar <= 'Z') ? std::string(1, firstChar) : "_";
        fs::path folder = fs::path(rootDir) / subDir;
        fs::create_directories(folder);

        fs::path filePath = folder / (term + ".txt");
        std::set<int> ids;

        if (fs::exists(filePath)) {
            std::ifstream in(filePath);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty()) {
                    try {
                        ids.insert(std::stoi(line));
                    } catch (...) {
                    }
                }
            }
        }

        ids.insert(bookId);

        std::ofstream out(filePath);
        if (out.is_open()) {
            for (int id : ids) {
                out << id << "\n";
            }
        }
    }

    return true;
}
