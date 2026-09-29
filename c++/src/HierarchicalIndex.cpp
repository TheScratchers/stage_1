#include "HierarchicalIndex.hpp"
#include "Tokenizer.hpp"
#include <fstream>
#include <filesystem>
#include <set>
#include <cctype>

namespace fs = std::filesystem;

bool HierarchicalIndex::update(int bookId, const std::string& bodyText, const std::string& rootDir) {
    for (const auto& term : Tokenizer::extractUniqueTokens(bodyText)) {
        char initial = std::toupper(static_cast<unsigned char>(term[0]));
        std::string subDir = (initial >= 'A' && initial <= 'Z') ? std::string(1, initial) : "_";
        fs::path folder = fs::path(rootDir) / subDir;
        std::error_code ec;
        fs::create_directories(folder, ec);

        fs::path filePath = folder / (term + ".txt");
        std::set<int> ids;

        if (fs::exists(filePath)) {
            std::ifstream in(filePath);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty()) {
                    try { ids.insert(std::stoi(line)); } catch (...) {}
                }
            }
        }

        ids.insert(bookId);

        std::ofstream out(filePath);
        if (out.is_open()) {
            for (int id : ids) out << id << "\n";
        }
    }
    return true;
}

std::vector<int> HierarchicalIndex::search(const std::string& term, const std::string& rootDir) {
    std::string lower = Tokenizer::toLower(term);
    if (lower.empty()) return {};

    char initial = std::toupper(static_cast<unsigned char>(lower[0]));
    std::string subDir = (initial >= 'A' && initial <= 'Z') ? std::string(1, initial) : "_";
    fs::path filePath = fs::path(rootDir) / subDir / (lower + ".txt");

    std::vector<int> ids;
    if (!fs::exists(filePath)) return ids;

    std::ifstream in(filePath);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            try { ids.push_back(std::stoi(line)); } catch (...) {}
        }
    }
    return ids;
}
