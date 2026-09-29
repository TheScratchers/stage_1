#include "InvertedIndex.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <numeric>

namespace fs = std::filesystem;

std::string InvertedIndex::structureToString(IndexStructureType type) {
    switch (type) {
        case IndexStructureType::MonolithicJson:    return "json_monolithic";
        case IndexStructureType::HierarchicalFiles: return "hierarchical";
        case IndexStructureType::BinaryCompact:      return "binary_compact";
    }
    return "unknown";
}

// Tokenizes text into lowercased alphabetic tokens.
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

// =========================================================================
// Structure 1: Monolithic JSON
// =========================================================================

// Fast streaming JSON parser for {"term": [id1, id2, ...], ...}
std::map<std::string, std::vector<int>> InvertedIndex::loadMonolithicIndex(const std::string& jsonPath) {
    std::map<std::string, std::vector<int>> index;
    std::ifstream file(jsonPath);
    if (!file.is_open()) return index;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t i = 0;
    size_t n = content.size();

    while (i < n) {
        // Find opening quote of key
        size_t quoteStart = content.find('"', i);
        if (quoteStart == std::string::npos) break;
        size_t quoteEnd = content.find('"', quoteStart + 1);
        if (quoteEnd == std::string::npos) break;

        std::string term = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

        // Find opening bracket of postings array
        size_t bracketOpen = content.find('[', quoteEnd + 1);
        if (bracketOpen == std::string::npos) break;
        size_t bracketClose = content.find(']', bracketOpen + 1);
        if (bracketClose == std::string::npos) break;

        std::string idsStr = content.substr(bracketOpen + 1, bracketClose - bracketOpen - 1);
        std::istringstream idStream(idsStr);
        std::string idToken;
        std::vector<int> ids;
        while (std::getline(idStream, idToken, ',')) {
            size_t start = idToken.find_first_not_of(" \t\r\n");
            size_t end = idToken.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                try {
                    ids.push_back(std::stoi(idToken.substr(start, end - start + 1)));
                } catch (...) {}
            }
        }

        index[term] = std::move(ids);
        i = bracketClose + 1;
    }

    return index;
}

bool InvertedIndex::saveMonolithicIndex(const std::map<std::string, std::vector<int>>& index, const std::string& jsonPath) {
    fs::path p(jsonPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    std::ofstream out(jsonPath);
    if (!out.is_open()) return false;

    out << "{\n";
    bool firstTerm = true;
    for (const auto& [term, ids] : index) {
        if (!firstTerm) out << ",\n";
        firstTerm = false;

        out << "  \"" << term << "\": [";
        for (size_t k = 0; k < ids.size(); ++k) {
            if (k > 0) out << ", ";
            out << ids[k];
        }
        out << "]";
    }
    out << "\n}\n";
    return true;
}

bool InvertedIndex::updateMonolithicIndex(int bookId, const std::string& bodyText, const std::string& jsonPath) {
    auto index = loadMonolithicIndex(jsonPath);
    std::set<std::string> terms = tokenize(bodyText);

    for (const auto& term : terms) {
        auto& ids = index[term];
        if (std::find(ids.begin(), ids.end(), bookId) == ids.end()) {
            ids.push_back(bookId);
            std::sort(ids.begin(), ids.end());
        }
    }

    return saveMonolithicIndex(index, jsonPath);
}

std::vector<int> InvertedIndex::searchMonolithic(const std::string& term, const std::string& jsonPath) {
    std::string lowerTerm;
    for (unsigned char c : term) lowerTerm += static_cast<char>(std::tolower(c));

    auto index = loadMonolithicIndex(jsonPath);
    auto it = index.find(lowerTerm);
    if (it != index.end()) {
        return it->second;
    }
    return {};
}

// =========================================================================
// Structure 2: Hierarchical Folders
// =========================================================================

bool InvertedIndex::updateHierarchicalIndex(int bookId, const std::string& bodyText, const std::string& rootDir) {
    std::set<std::string> terms = tokenize(bodyText);

    for (const auto& term : terms) {
        char firstChar = std::toupper(static_cast<unsigned char>(term[0]));
        std::string subDir = (firstChar >= 'A' && firstChar <= 'Z') ? std::string(1, firstChar) : "_";
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
                    try {
                        ids.insert(std::stoi(line));
                    } catch (...) {}
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

std::vector<int> InvertedIndex::searchHierarchical(const std::string& term, const std::string& rootDir) {
    std::string lowerTerm;
    for (unsigned char c : term) lowerTerm += static_cast<char>(std::tolower(c));
    if (lowerTerm.empty()) return {};

    char firstChar = std::toupper(static_cast<unsigned char>(lowerTerm[0]));
    std::string subDir = (firstChar >= 'A' && firstChar <= 'Z') ? std::string(1, firstChar) : "_";
    fs::path filePath = fs::path(rootDir) / subDir / (lowerTerm + ".txt");

    std::vector<int> ids;
    if (!fs::exists(filePath)) return ids;

    std::ifstream in(filePath);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            try {
                ids.push_back(std::stoi(line));
            } catch (...) {}
        }
    }
    return ids;
}

// =========================================================================
// Structure 3: Custom Binary Compact Index (Magic: "BIDX")
// Header (17 bytes):
//   Magic: 4 bytes ("BIDX")
//   Version: 1 byte (1)
//   Term Count: uint32_t (4 bytes)
//   Dictionary Offset: uint64_t (8 bytes)
// Postings Section:
//   Contiguous array of uint32_t book IDs
// Dictionary Section (at Dictionary Offset):
//   For each term:
//     termLen: uint16_t
//     termBytes: char[termLen]
//     postingsOffset: uint64_t
//     postingsCount: uint32_t
// =========================================================================

bool InvertedIndex::buildBinaryIndexFromMap(const std::map<std::string, std::vector<int>>& indexMap, const std::string& binPath) {
    fs::path p(binPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    std::ofstream out(binPath, std::ios::binary);
    if (!out.is_open()) return false;

    // Header placeholder
    const char magic[4] = {'B', 'I', 'D', 'X'};
    const uint8_t version = 1;
    uint32_t termCount = static_cast<uint32_t>(indexMap.size());
    uint64_t dictOffsetPlaceholder = 0;

    out.write(magic, 4);
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&termCount), sizeof(termCount));
    out.write(reinterpret_cast<const char*>(&dictOffsetPlaceholder), sizeof(dictOffsetPlaceholder));

    // Postings section
    struct TermDictEntry {
        std::string term;
        uint64_t offset;
        uint32_t count;
    };
    std::vector<TermDictEntry> dictEntries;
    dictEntries.reserve(termCount);

    for (const auto& [term, ids] : indexMap) {
        uint64_t offset = static_cast<uint64_t>(out.tellp());
        uint32_t count = static_cast<uint32_t>(ids.size());

        for (int id : ids) {
            uint32_t uId = static_cast<uint32_t>(id);
            out.write(reinterpret_cast<const char*>(&uId), sizeof(uId));
        }

        dictEntries.push_back({term, offset, count});
    }

    // Dictionary section
    uint64_t actualDictOffset = static_cast<uint64_t>(out.tellp());
    for (const auto& entry : dictEntries) {
        uint16_t len = static_cast<uint16_t>(entry.term.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(entry.term.data(), len);
        out.write(reinterpret_cast<const char*>(&entry.offset), sizeof(entry.offset));
        out.write(reinterpret_cast<const char*>(&entry.count), sizeof(entry.count));
    }

    // Rewind and patch dictionary offset in header
    out.seekp(4 + 1 + sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&actualDictOffset), sizeof(actualDictOffset));

    return true;
}

bool InvertedIndex::updateBinaryIndex(int bookId, const std::string& bodyText, const std::string& binPath) {
    // Read existing map from binary index if file exists
    std::map<std::string, std::vector<int>> index;
    if (fs::exists(binPath)) {
        std::ifstream in(binPath, std::ios::binary);
        if (in.is_open()) {
            char magic[4];
            in.read(magic, 4);
            if (std::memcmp(magic, "BIDX", 4) == 0) {
                uint8_t version = 0;
                uint32_t termCount = 0;
                uint64_t dictOffset = 0;
                in.read(reinterpret_cast<char*>(&version), sizeof(version));
                in.read(reinterpret_cast<char*>(&termCount), sizeof(termCount));
                in.read(reinterpret_cast<char*>(&dictOffset), sizeof(dictOffset));

                in.seekg(dictOffset);
                struct TempEntry {
                    std::string term;
                    uint64_t offset;
                    uint32_t count;
                };
                std::vector<TempEntry> entries;
                entries.reserve(termCount);

                for (uint32_t k = 0; k < termCount; ++k) {
                    uint16_t len = 0;
                    in.read(reinterpret_cast<char*>(&len), sizeof(len));
                    std::string term(len, '\0');
                    in.read(&term[0], len);
                    uint64_t off = 0;
                    uint32_t cnt = 0;
                    in.read(reinterpret_cast<char*>(&off), sizeof(off));
                    in.read(reinterpret_cast<char*>(&cnt), sizeof(cnt));
                    entries.push_back({term, off, cnt});
                }

                for (const auto& e : entries) {
                    in.seekg(e.offset);
                    std::vector<int> ids(e.count);
                    for (uint32_t c = 0; c < e.count; ++c) {
                        uint32_t idVal = 0;
                        in.read(reinterpret_cast<char*>(&idVal), sizeof(idVal));
                        ids[c] = static_cast<int>(idVal);
                    }
                    index[e.term] = std::move(ids);
                }
            }
        }
    }

    // Merge new book tokens
    std::set<std::string> terms = tokenize(bodyText);
    for (const auto& term : terms) {
        auto& ids = index[term];
        if (std::find(ids.begin(), ids.end(), bookId) == ids.end()) {
            ids.push_back(bookId);
            std::sort(ids.begin(), ids.end());
        }
    }

    return buildBinaryIndexFromMap(index, binPath);
}

std::vector<int> InvertedIndex::searchBinary(const std::string& term, const std::string& binPath) {
    std::string lowerTerm;
    for (unsigned char c : term) lowerTerm += static_cast<char>(std::tolower(c));
    if (lowerTerm.empty() || !fs::exists(binPath)) return {};

    std::ifstream in(binPath, std::ios::binary);
    if (!in.is_open()) return {};

    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, "BIDX", 4) != 0) return {};

    uint8_t version = 0;
    uint32_t termCount = 0;
    uint64_t dictOffset = 0;

    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&termCount), sizeof(termCount));
    in.read(reinterpret_cast<char*>(&dictOffset), sizeof(dictOffset));

    in.seekg(dictOffset);
    uint64_t matchedOffset = 0;
    uint32_t matchedCount = 0;
    bool found = false;

    for (uint32_t i = 0; i < termCount; ++i) {
        uint16_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string currentTerm(len, '\0');
        in.read(&currentTerm[0], len);
        uint64_t off = 0;
        uint32_t cnt = 0;
        in.read(reinterpret_cast<char*>(&off), sizeof(off));
        in.read(reinterpret_cast<char*>(&cnt), sizeof(cnt));

        if (currentTerm == lowerTerm) {
            matchedOffset = off;
            matchedCount = cnt;
            found = true;
            break;
        }
    }

    if (!found || matchedCount == 0) return {};

    // Direct seek to postings offset: reads only the target term's postings
    in.seekg(matchedOffset);
    std::vector<int> results(matchedCount);
    for (uint32_t i = 0; i < matchedCount; ++i) {
        uint32_t idVal = 0;
        in.read(reinterpret_cast<char*>(&idVal), sizeof(idVal));
        results[i] = static_cast<int>(idVal);
    }

    return results;
}

// =========================================================================
// Unified Query Engine
// =========================================================================

std::vector<int> InvertedIndex::querySingle(const std::string& term, IndexStructureType type, const std::string& basePath) {
    switch (type) {
        case IndexStructureType::MonolithicJson:    return searchMonolithic(term, basePath);
        case IndexStructureType::HierarchicalFiles: return searchHierarchical(term, basePath);
        case IndexStructureType::BinaryCompact:      return searchBinary(term, basePath);
    }
    return {};
}

std::vector<int> InvertedIndex::queryAnd(const std::vector<std::string>& terms, IndexStructureType type, const std::string& basePath) {
    if (terms.empty()) return {};

    std::vector<int> result = querySingle(terms[0], type, basePath);
    for (size_t i = 1; i < terms.size() && !result.empty(); ++i) {
        std::vector<int> nextResult = querySingle(terms[i], type, basePath);
        std::vector<int> intersection;
        std::set_intersection(
            result.begin(), result.end(),
            nextResult.begin(), nextResult.end(),
            std::back_inserter(intersection)
        );
        result = std::move(intersection);
    }
    return result;
}

std::vector<int> InvertedIndex::queryOr(const std::vector<std::string>& terms, IndexStructureType type, const std::string& basePath) {
    std::set<int> unionSet;
    for (const auto& term : terms) {
        std::vector<int> postings = querySingle(term, type, basePath);
        for (int id : postings) {
            unionSet.insert(id);
        }
    }
    return std::vector<int>(unionSet.begin(), unionSet.end());
}

// =========================================================================
// Benchmarking Suite
// =========================================================================

static uintmax_t calculateDirSize(const fs::path& root) {
    if (!fs::exists(root)) return 0;
    if (fs::is_regular_file(root)) return fs::file_size(root);
    uintmax_t size = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (entry.is_regular_file()) {
            size += entry.file_size();
        }
    }
    return size;
}

std::vector<IndexBenchmarkResult> InvertedIndex::runBenchmark(
    const fs::path& benchDir,
    const std::map<int, std::string>& bookBodies,
    const std::vector<std::string>& queryTerms,
    const std::string& outputJsonPath
) {
    std::vector<IndexBenchmarkResult> results;
    if (bookBodies.empty()) return results;

    std::error_code ec;
    fs::create_directories(benchDir, ec);

    // Extract term vocabulary map in memory first to share across builders
    std::map<std::string, std::vector<int>> fullIndexMap;
    for (const auto& [bookId, body] : bookBodies) {
        std::set<std::string> words = tokenize(body);
        for (const auto& w : words) {
            fullIndexMap[w].push_back(bookId);
        }
    }
    for (auto& [_, ids] : fullIndexMap) {
        std::sort(ids.begin(), ids.end());
    }

    int totalTerms = static_cast<int>(fullIndexMap.size());
    int totalBooks = static_cast<int>(bookBodies.size());

    // -------------------------------------------------------------------------
    // 1. Benchmark Monolithic JSON
    // -------------------------------------------------------------------------
    {
        IndexBenchmarkResult r;
        r.structureName = "json_monolithic";
        r.numTerms = totalTerms;
        r.numBooks = totalBooks;

        fs::path jsonPath = benchDir / "inverted_index.json";
        fs::remove(jsonPath, ec);

        auto t0 = std::chrono::high_resolution_clock::now();
        saveMonolithicIndex(fullIndexMap, jsonPath.string());
        auto t1 = std::chrono::high_resolution_clock::now();
        r.buildSeconds = std::chrono::duration<double>(t1 - t0).count();

        // Cold load cost
        t0 = std::chrono::high_resolution_clock::now();
        auto loaded = loadMonolithicIndex(jsonPath.string());
        t1 = std::chrono::high_resolution_clock::now();
        r.coldLoadSeconds = std::chrono::duration<double>(t1 - t0).count();

        // Lookup cost in-memory
        if (!queryTerms.empty()) {
            t0 = std::chrono::high_resolution_clock::now();
            for (const auto& q : queryTerms) {
                auto it = loaded.find(q);
                if (it != loaded.end()) {
                    (void)it->second.size();
                }
            }
            t1 = std::chrono::high_resolution_clock::now();
            double totalLookupSecs = std::chrono::duration<double>(t1 - t0).count();
            r.avgLookupMs = (totalLookupSecs / queryTerms.size()) * 1000.0;
        }

        r.numFiles = 1;
        r.numDirs = 0;
        r.totalSizeBytes = fs::file_size(jsonPath);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // 2. Benchmark Hierarchical Folders
    // -------------------------------------------------------------------------
    {
        IndexBenchmarkResult r;
        r.structureName = "hierarchical";
        r.numTerms = totalTerms;
        r.numBooks = totalBooks;

        fs::path hierDir = benchDir / "inverted_index_hier";
        fs::remove_all(hierDir, ec);
        fs::create_directories(hierDir, ec);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (const auto& [term, ids] : fullIndexMap) {
            char firstChar = std::toupper(static_cast<unsigned char>(term[0]));
            std::string subDir = (firstChar >= 'A' && firstChar <= 'Z') ? std::string(1, firstChar) : "_";
            fs::path folder = hierDir / subDir;
            fs::create_directories(folder, ec);
            std::ofstream out(folder / (term + ".txt"));
            for (int id : ids) out << id << "\n";
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        r.buildSeconds = std::chrono::duration<double>(t1 - t0).count();
        r.coldLoadSeconds = 0.0; // No cold load step; opened per file on demand

        // Lookup cost: reading individual files from disk
        if (!queryTerms.empty()) {
            t0 = std::chrono::high_resolution_clock::now();
            for (const auto& q : queryTerms) {
                searchHierarchical(q, hierDir.string());
            }
            t1 = std::chrono::high_resolution_clock::now();
            double totalLookupSecs = std::chrono::duration<double>(t1 - t0).count();
            r.avgLookupMs = (totalLookupSecs / queryTerms.size()) * 1000.0;
        }

        int fileCount = 0;
        int dirCount = 0;
        for (const auto& entry : fs::recursive_directory_iterator(hierDir, ec)) {
            if (entry.is_regular_file()) fileCount++;
            else if (entry.is_directory()) dirCount++;
        }
        r.numFiles = fileCount;
        r.numDirs = dirCount;
        r.totalSizeBytes = calculateDirSize(hierDir);
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // 3. Benchmark Custom Binary Compact Index
    // -------------------------------------------------------------------------
    {
        IndexBenchmarkResult r;
        r.structureName = "binary_compact";
        r.numTerms = totalTerms;
        r.numBooks = totalBooks;

        fs::path binPath = benchDir / "inverted_index.bin";
        fs::remove(binPath, ec);

        auto t0 = std::chrono::high_resolution_clock::now();
        buildBinaryIndexFromMap(fullIndexMap, binPath.string());
        auto t1 = std::chrono::high_resolution_clock::now();
        r.buildSeconds = std::chrono::duration<double>(t1 - t0).count();
        r.coldLoadSeconds = 0.0;

        // Direct random seek lookup cost from disk
        if (!queryTerms.empty()) {
            t0 = std::chrono::high_resolution_clock::now();
            for (const auto& q : queryTerms) {
                searchBinary(q, binPath.string());
            }
            t1 = std::chrono::high_resolution_clock::now();
            double totalLookupSecs = std::chrono::duration<double>(t1 - t0).count();
            r.avgLookupMs = (totalLookupSecs / queryTerms.size()) * 1000.0;
        }

        r.numFiles = 1;
        r.numDirs = 0;
        r.totalSizeBytes = fs::file_size(binPath);
        results.push_back(r);
    }

    // Export to JSON if specified
    if (!outputJsonPath.empty()) {
        fs::path outPath(outputJsonPath);
        if (outPath.has_parent_path()) {
            fs::create_directories(outPath.parent_path(), ec);
        }
        std::ofstream jsonFile(outPath);
        if (jsonFile.is_open()) {
            jsonFile << "{\n";
            for (size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                jsonFile << "  \"" << r.structureName << "\": {\n";
                jsonFile << "    \"structure\": \"" << r.structureName << "\",\n";
                jsonFile << "    \"n_terms\": " << r.numTerms << ",\n";
                jsonFile << "    \"build_seconds\": " << std::fixed << std::setprecision(4) << r.buildSeconds << ",\n";
                if (r.structureName == "json_monolithic") {
                    jsonFile << "    \"cold_load_seconds\": " << std::fixed << std::setprecision(4) << r.coldLoadSeconds << ",\n";
                    jsonFile << "    \"in_memory_lookup_avg_ms\": " << std::fixed << std::setprecision(5) << r.avgLookupMs << ",\n";
                } else {
                    jsonFile << "    \"avg_lookup_ms\": " << std::fixed << std::setprecision(5) << r.avgLookupMs << ",\n";
                }
                jsonFile << "    \"num_files\": " << r.numFiles << ",\n";
                jsonFile << "    \"num_dirs\": " << r.numDirs << ",\n";
                jsonFile << "    \"total_size_bytes\": " << r.totalSizeBytes << "\n";
                jsonFile << "  }" << (i + 1 < results.size() ? "," : "") << "\n";
            }
            jsonFile << "}\n";
        }
    }

    return results;
}
