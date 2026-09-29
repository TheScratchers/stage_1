#include "BinaryIndex.hpp"
#include "Tokenizer.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;

bool BinaryIndex::buildFromMap(const std::map<std::string, std::vector<int>>& indexMap, const std::string& binPath) {
    fs::path p(binPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }

    std::ofstream out(binPath, std::ios::binary);
    if (!out.is_open()) return false;

    // Header: Magic "BIDX" (4B), version 1 (1B), termCount (4B), dictOffset placeholder (8B)
    const char magic[4] = {'B', 'I', 'D', 'X'};
    const uint8_t version = 1;
    uint32_t termCount = static_cast<uint32_t>(indexMap.size());
    uint64_t dictOffsetPlaceholder = 0;

    out.write(magic, 4);
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&termCount), sizeof(termCount));
    out.write(reinterpret_cast<const char*>(&dictOffsetPlaceholder), sizeof(dictOffsetPlaceholder));

    // Postings section
    struct DictRecord {
        std::string term;
        uint64_t offset;
        uint32_t count;
    };
    std::vector<DictRecord> dictRecords;
    dictRecords.reserve(termCount);

    for (const auto& [term, ids] : indexMap) {
        uint64_t offset = static_cast<uint64_t>(out.tellp());
        uint32_t count = static_cast<uint32_t>(ids.size());

        for (int id : ids) {
            uint32_t uid = static_cast<uint32_t>(id);
            out.write(reinterpret_cast<const char*>(&uid), sizeof(uid));
        }
        dictRecords.push_back({term, offset, count});
    }

    // Dictionary section
    uint64_t actualDictOffset = static_cast<uint64_t>(out.tellp());
    for (const auto& r : dictRecords) {
        uint16_t len = static_cast<uint16_t>(r.term.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(r.term.data(), len);
        out.write(reinterpret_cast<const char*>(&r.offset), sizeof(r.offset));
        out.write(reinterpret_cast<const char*>(&r.count), sizeof(r.count));
    }

    // Patch dictOffset in header
    out.seekp(4 + 1 + sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&actualDictOffset), sizeof(actualDictOffset));

    return true;
}

std::map<std::string, std::vector<int>> BinaryIndex::load(const std::string& binPath) {
    std::map<std::string, std::vector<int>> index;
    if (!fs::exists(binPath)) return index;

    std::ifstream in(binPath, std::ios::binary);
    if (!in.is_open()) return index;

    char magic[4];
    in.read(magic, 4);
    if (std::memcmp(magic, "BIDX", 4) != 0) return index;

    uint8_t version = 0;
    uint32_t termCount = 0;
    uint64_t dictOffset = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&termCount), sizeof(termCount));
    in.read(reinterpret_cast<char*>(&dictOffset), sizeof(dictOffset));

    in.seekg(dictOffset);
    struct Entry { std::string term; uint64_t offset; uint32_t count; };
    std::vector<Entry> entries;
    entries.reserve(termCount);

    for (uint32_t i = 0; i < termCount; ++i) {
        uint16_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string term(len, '\0');
        in.read(&term[0], len);
        uint64_t off = 0; uint32_t cnt = 0;
        in.read(reinterpret_cast<char*>(&off), sizeof(off));
        in.read(reinterpret_cast<char*>(&cnt), sizeof(cnt));
        entries.push_back({term, off, cnt});
    }

    for (const auto& e : entries) {
        in.clear();
        in.seekg(e.offset);
        std::vector<int> ids(e.count);
        for (uint32_t c = 0; c < e.count; ++c) {
            uint32_t uid = 0;
            in.read(reinterpret_cast<char*>(&uid), sizeof(uid));
            ids[c] = static_cast<int>(uid);
        }
        index[e.term] = std::move(ids);
    }

    return index;
}

bool BinaryIndex::update(int bookId, const std::string& bodyText, const std::string& binPath) {
    auto index = load(binPath);
    for (const auto& term : Tokenizer::extractUniqueTokens(bodyText)) {
        auto& ids = index[term];
        if (std::find(ids.begin(), ids.end(), bookId) == ids.end()) {
            ids.push_back(bookId);
            std::sort(ids.begin(), ids.end());
        }
    }
    return buildFromMap(index, binPath);
}

std::vector<int> BinaryIndex::search(const std::string& term, const std::string& binPath) {
    std::string lower = Tokenizer::toLower(term);
    if (lower.empty() || !fs::exists(binPath)) return {};

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
        uint64_t off = 0; uint32_t cnt = 0;
        in.read(reinterpret_cast<char*>(&off), sizeof(off));
        in.read(reinterpret_cast<char*>(&cnt), sizeof(cnt));

        if (currentTerm == lower) {
            matchedOffset = off;
            matchedCount = cnt;
            found = true;
            break;
        }
    }

    if (!found || matchedCount == 0) return {};

    in.clear();
    in.seekg(matchedOffset);
    std::vector<int> results(matchedCount);
    for (uint32_t i = 0; i < matchedCount; ++i) {
        uint32_t uid = 0;
        in.read(reinterpret_cast<char*>(&uid), sizeof(uid));
        results[i] = static_cast<int>(uid);
    }
    return results;
}
