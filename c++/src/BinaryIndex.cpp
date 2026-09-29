#include "BinaryIndex.hpp"
#include "Tokenizer.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;

static bool readHeader(std::ifstream& in, uint32_t& termCount, uint64_t& dictOffset) {
    char magic[4]; uint8_t ver = 0;
    if (!in.read(magic, 4) || std::memcmp(magic, "BIDX", 4) != 0) return false;
    return in.read(reinterpret_cast<char*>(&ver), 1) &&
           in.read(reinterpret_cast<char*>(&termCount), 4) &&
           in.read(reinterpret_cast<char*>(&dictOffset), 8);
}

bool BinaryIndex::buildFromMap(const std::map<std::string, std::vector<int>>& indexMap, const std::string& binPath) {
    fs::path p(binPath);
    if (p.has_parent_path()) { std::error_code ec; fs::create_directories(p.parent_path(), ec); }
    std::ofstream out(binPath, std::ios::binary);
    if (!out.is_open()) return false;

    const char magic[4] = {'B', 'I', 'D', 'X'};
    const uint8_t ver = 1;
    uint32_t termCount = static_cast<uint32_t>(indexMap.size());
    uint64_t dictOffsetZero = 0;

    out.write(magic, 4);
    out.write(reinterpret_cast<const char*>(&ver), 1);
    out.write(reinterpret_cast<const char*>(&termCount), 4);
    out.write(reinterpret_cast<const char*>(&dictOffsetZero), 8);

    struct DictRec { std::string term; uint64_t off; uint32_t cnt; };
    std::vector<DictRec> dict;
    dict.reserve(termCount);

    for (const auto& [term, ids] : indexMap) {
        dict.push_back({term, static_cast<uint64_t>(out.tellp()), static_cast<uint32_t>(ids.size())});
        for (int id : ids) {
            uint32_t uid = static_cast<uint32_t>(id);
            out.write(reinterpret_cast<const char*>(&uid), 4);
        }
    }
    uint64_t actualDictOffset = static_cast<uint64_t>(out.tellp());
    for (const auto& r : dict) {
        uint16_t len = static_cast<uint16_t>(r.term.size());
        out.write(reinterpret_cast<const char*>(&len), 2);
        out.write(r.term.data(), len);
        out.write(reinterpret_cast<const char*>(&r.off), 8);
        out.write(reinterpret_cast<const char*>(&r.cnt), 4);
    }
    out.seekp(4 + 1 + 4);
    out.write(reinterpret_cast<const char*>(&actualDictOffset), 8);
    return true;
}

std::map<std::string, std::vector<int>> BinaryIndex::load(const std::string& binPath) {
    std::map<std::string, std::vector<int>> index;
    if (!fs::exists(binPath)) return index;
    std::ifstream in(binPath, std::ios::binary);
    uint32_t termCount = 0; uint64_t dictOffset = 0;
    if (!in.is_open() || !readHeader(in, termCount, dictOffset)) return index;

    in.seekg(dictOffset);
    struct Entry { std::string term; uint64_t off; uint32_t cnt; };
    std::vector<Entry> entries(termCount);
    for (uint32_t i = 0; i < termCount; ++i) {
        uint16_t len = 0;
        in.read(reinterpret_cast<char*>(&len), 2);
        entries[i].term.resize(len);
        in.read(&entries[i].term[0], len);
        in.read(reinterpret_cast<char*>(&entries[i].off), 8);
        in.read(reinterpret_cast<char*>(&entries[i].cnt), 4);
    }
    for (const auto& e : entries) {
        in.clear();
        in.seekg(e.off);
        std::vector<int> ids(e.cnt);
        for (uint32_t c = 0; c < e.cnt; ++c) {
            uint32_t uid = 0;
            in.read(reinterpret_cast<char*>(&uid), 4);
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
    uint32_t termCount = 0; uint64_t dictOffset = 0;
    if (!in.is_open() || !readHeader(in, termCount, dictOffset)) return {};

    in.seekg(dictOffset);
    for (uint32_t i = 0; i < termCount; ++i) {
        uint16_t len = 0;
        in.read(reinterpret_cast<char*>(&len), 2);
        std::string cur(len, '\0');
        in.read(&cur[0], len);
        uint64_t off = 0; uint32_t cnt = 0;
        in.read(reinterpret_cast<char*>(&off), 8);
        in.read(reinterpret_cast<char*>(&cnt), 4);

        if (cur == lower) {
            in.clear();
            in.seekg(off);
            std::vector<int> res(cnt);
            for (uint32_t c = 0; c < cnt; ++c) {
                uint32_t uid = 0;
                in.read(reinterpret_cast<char*>(&uid), 4);
                res[c] = static_cast<int>(uid);
            }
            return res;
        }
    }
    return {};
}
