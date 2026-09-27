#include "MetadataExtractor.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;

// Parses title, author, and language from a Project Gutenberg header string.
BookMetadata MetadataExtractor::parseHeader(int bookId, const std::string& headerText) {
    BookMetadata meta;
    meta.bookId = bookId;
    meta.title = "Unknown";
    meta.author = "Unknown";
    meta.language = "Unknown";

    std::regex titleRegex(R"(^Title:\s*(.+)$)", std::regex::icase);
    std::regex authorRegex(R"(^Author:\s*(.+)$)", std::regex::icase);
    std::regex langRegex(R"(^Language:\s*(.+)$)", std::regex::icase);

    std::istringstream stream(headerText);
    std::string line;
    std::smatch match;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (meta.title == "Unknown" && std::regex_match(line, match, titleRegex)) {
            meta.title = match[1].str();
        } else if (meta.author == "Unknown" && std::regex_match(line, match, authorRegex)) {
            meta.author = match[1].str();
        } else if (meta.language == "Unknown" && std::regex_match(line, match, langRegex)) {
            meta.language = match[1].str();
        }
    }

    return meta;
}

// Appends the book metadata to a CSV file in the datamart directory.
bool MetadataExtractor::saveToCsv(const BookMetadata& metadata, const std::string& csvPath) {
    fs::path path(csvPath);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }

    bool fileExists = fs::exists(path);
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) {
        return false;
    }

    if (!fileExists) {
        out << "book_id,title,author,language\n";
    }

    // Helper lambda to escape quotes in CSV fields
    auto escapeCsv = [](const std::string& field) {
        std::string res = "\"";
        for (char c : field) {
            if (c == '"') res += "\"\"";
            else res += c;
        }
        res += "\"";
        return res;
    };

    out << metadata.bookId << ","
        << escapeCsv(metadata.title) << ","
        << escapeCsv(metadata.author) << ","
        << escapeCsv(metadata.language) << "\n";

    return true;
}
