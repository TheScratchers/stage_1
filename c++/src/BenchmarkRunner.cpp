#include "BenchmarkRunner.hpp"
#include "Datalake.hpp"
#include "JsonIndex.hpp"
#include "HierarchicalIndex.hpp"
#include "BinaryIndex.hpp"
#include "Tokenizer.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <random>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::map<int, std::pair<std::string, std::string>> loadSampleBooks(const fs::path& datalakePath) {
    std::map<int, std::pair<std::string, std::string>> sampleData;
    int realIds[] = {5, 11, 84, 1342};
    for (int id : realIds) {
        BookFiles f = Datalake::findBookRecursive(datalakePath, id);
        if (f.exists) {
            std::ifstream hs(f.headerPath);
            std::string h((std::istreambuf_iterator<char>(hs)), std::istreambuf_iterator<char>());
            std::ifstream bs(f.bodyPath);
            std::string b((std::istreambuf_iterator<char>(bs)), std::istreambuf_iterator<char>());
            sampleData[id] = {h, b};
        }
    }
    if (sampleData.empty()) {
        sampleData[1342] = {
            "Title: Pride and Prejudice\nAuthor: Jane Austen\nLanguage: English\n",
            "It is a truth universally acknowledged that a single man in possession of a good fortune must be in want of a wife."
        };
        sampleData[11] = {
            "Title: Alice's Adventures in Wonderland\nAuthor: Lewis Carroll\nLanguage: English\n",
            "Alice was beginning to get very tired of sitting by her sister on the bank and having nothing to do."
        };
        sampleData[84] = {
            "Title: Frankenstein\nAuthor: Mary Shelley\nLanguage: English\n",
            "You will rejoice to hear that no disaster has accompanied the commencement of an enterprise."
        };
    }
    return sampleData;
}

void BenchmarkRunner::runDatalakeBenchmark(
    const fs::path& benchDir,
    const fs::path& datalakePath,
    int nBooks,
    const std::string& outJsonPath
) {
    std::cout << "\n=================================================================\n"
              << " Running Datalake Benchmark (" << nBooks << " synthetic books)\n"
              << "=================================================================\n";

    auto sampleBooks = loadSampleBooks(datalakePath);
    std::vector<int> sampleIds;
    for (const auto& [id, _] : sampleBooks) sampleIds.push_back(id);

    std::vector<DatalakeLayout> layouts = {
        DatalakeLayout::TimeBased,
        DatalakeLayout::BookBased,
        DatalakeLayout::BatchBased
    };

    std::vector<int> bookIds;
    for (int i = 0; i < nBooks; ++i) bookIds.push_back(100000 + i);

    std::vector<int> lookupSample = bookIds;
    std::mt19937 rng(42);
    std::shuffle(lookupSample.begin(), lookupSample.end(), rng);
    if (lookupSample.size() > 100) lookupSample.resize(100);

    json resultsJson;
    resultsJson["n_books"] = nBooks;
    resultsJson["results"] = json::array();

    std::cout << std::left
              << std::setw(15) << "Structure"
              << std::setw(12) << "Write (s)"
              << std::setw(12) << "Books/sec"
              << std::setw(16) << "Lookup avg(ms)"
              << std::setw(10) << "# Dirs"
              << std::setw(12) << "Max Depth"
              << std::setw(14) << "Avg Files/Dir\n";
    std::cout << std::string(81, '-') << "\n";

    for (auto layout : layouts) {
        std::string name = Datalake::layoutToString(layout);
        fs::path target = benchDir / name;
        std::error_code ec;
        fs::remove_all(target, ec);
        fs::create_directories(target, ec);

        // Write
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < nBooks; ++i) {
            int sid = sampleIds[i % sampleIds.size()];
            const auto& [h, b] = sampleBooks.at(sid);
            Datalake::saveBook(target, layout, bookIds[i], h, b);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double writeSec = std::chrono::duration<double>(t1 - t0).count();
        double throughput = (writeSec > 0) ? (nBooks / writeSec) : 0.0;

        // Lookup
        t0 = std::chrono::high_resolution_clock::now();
        for (int id : lookupSample) {
            Datalake::locateBook(target, layout, id);
        }
        t1 = std::chrono::high_resolution_clock::now();
        double lookupSec = std::chrono::duration<double>(t1 - t0).count();
        double lookupAvgMs = (lookupSample.empty()) ? 0.0 : ((lookupSec / lookupSample.size()) * 1000.0);

        // Overhead
        int numDirs = 0, maxDepth = 0;
        std::map<fs::path, int> filesPerDir;
        for (const auto& entry : fs::recursive_directory_iterator(target, ec)) {
            if (entry.is_directory()) {
                numDirs++;
                auto rel = fs::relative(entry.path(), target);
                int depth = std::distance(rel.begin(), rel.end());
                if (depth > maxDepth) maxDepth = depth;
            } else if (entry.is_regular_file()) {
                filesPerDir[entry.path().parent_path()]++;
            }
        }
        double avgFiles = filesPerDir.empty() ? 0 : (double)nBooks * 2 / filesPerDir.size();

        std::cout << std::left
                  << std::setw(15) << name
                  << std::setw(12) << std::fixed << std::setprecision(4) << writeSec
                  << std::setw(12) << std::fixed << std::setprecision(1) << throughput
                  << std::setw(16) << std::fixed << std::setprecision(4) << lookupAvgMs
                  << std::setw(10) << numDirs
                  << std::setw(12) << maxDepth
                  << std::setw(14) << std::fixed << std::setprecision(1) << avgFiles << "\n";

        json item;
        item["structure"] = name;
        item["n_books"] = nBooks;
        item["write_seconds"] = writeSec;
        item["write_books_per_sec"] = throughput;
        item["lookup_n"] = (int)lookupSample.size();
        item["lookup_seconds"] = lookupSec;
        item["lookup_avg_ms"] = lookupAvgMs;
        item["num_dirs_created"] = numDirs;
        item["max_depth"] = maxDepth;
        item["avg_files_per_dir"] = avgFiles;
        resultsJson["results"].push_back(item);

        fs::remove_all(target, ec);
    }

    if (!outJsonPath.empty()) {
        fs::path p(outJsonPath);
        if (p.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }
        std::ofstream f(outJsonPath);
        f << resultsJson.dump(2) << "\n";
        std::cout << "\nResults saved to: " << outJsonPath << "\n";
    }
}

void BenchmarkRunner::runIndexBenchmark(
    const fs::path& benchDir,
    const fs::path& datalakePath,
    int nBooks,
    const std::string& outJsonPath
) {
    std::cout << "\n=================================================================\n"
              << " Running Inverted Index Benchmark (3 Structures)\n"
              << "=================================================================\n";

    auto sampleBooks = loadSampleBooks(datalakePath);
    std::vector<std::string> realTexts;
    for (const auto& [_, pair] : sampleBooks) {
        realTexts.push_back(pair.second);
    }
    if (realTexts.empty()) {
        realTexts.push_back("The quick brown fox jumps over the lazy dog in the sunny park.");
    }

    std::map<int, std::string> bookBodies;
    for (int i = 0; i < nBooks; ++i) {
        bookBodies[200000 + i] = realTexts[i % realTexts.size()];
    }

    // Build unified terms map
    std::map<std::string, std::vector<int>> fullMap;
    for (const auto& [id, body] : bookBodies) {
        for (const auto& term : Tokenizer::extractUniqueTokens(body)) {
            fullMap[term].push_back(id);
        }
    }
    for (auto& [_, ids] : fullMap) std::sort(ids.begin(), ids.end());

    std::vector<std::string> querySample = {"truth", "fortune", "wife", "sister", "alice", "man"};
    json out;

    std::cout << std::left
              << std::setw(20) << "Structure"
              << std::setw(12) << "Build (s)"
              << std::setw(18) << "Avg Lookup (ms)"
              << std::setw(10) << "Files"
              << std::setw(10) << "Dirs"
              << std::setw(14) << "Size (KB)\n";
    std::cout << std::string(84, '-') << "\n";

    std::error_code ec;
    fs::create_directories(benchDir, ec);

    // 1. JSON
    {
        fs::path jsonFile = benchDir / "inverted_index.json";
        auto t0 = std::chrono::high_resolution_clock::now();
        JsonIndex::save(fullMap, jsonFile.string());
        auto t1 = std::chrono::high_resolution_clock::now();
        double buildSec = std::chrono::duration<double>(t1 - t0).count();

        t0 = std::chrono::high_resolution_clock::now();
        auto loaded = JsonIndex::load(jsonFile.string());
        t1 = std::chrono::high_resolution_clock::now();
        double coldSec = std::chrono::duration<double>(t1 - t0).count();

        t0 = std::chrono::high_resolution_clock::now();
        for (const auto& q : querySample) loaded.find(q);
        t1 = std::chrono::high_resolution_clock::now();
        double lookupMs = (std::chrono::duration<double>(t1 - t0).count() / querySample.size()) * 1000.0;
        double kb = fs::file_size(jsonFile) / 1024.0;

        std::cout << std::left << std::setw(20) << "json_monolithic"
                  << std::setw(12) << std::fixed << std::setprecision(4) << buildSec
                  << std::setw(18) << std::fixed << std::setprecision(4) << lookupMs
                  << std::setw(10) << 1 << std::setw(10) << 0
                  << std::setw(14) << std::fixed << std::setprecision(1) << kb << "\n";

        out["json_monolithic"] = {
            {"structure", "json_monolithic"}, {"n_terms", fullMap.size()},
            {"build_seconds", buildSec}, {"cold_load_seconds", coldSec},
            {"in_memory_lookup_avg_ms", lookupMs}, {"num_files", 1},
            {"num_dirs", 0}, {"total_size_bytes", fs::file_size(jsonFile)}
        };
    }

    // 2. Hierarchical
    {
        fs::path hierDir = benchDir / "inverted_index_hier";
        fs::remove_all(hierDir, ec);
        fs::create_directories(hierDir, ec);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (const auto& [term, ids] : fullMap) {
            char ini = std::toupper(static_cast<unsigned char>(term[0]));
            std::string sub = (ini >= 'A' && ini <= 'Z') ? std::string(1, ini) : "_";
            fs::path f = hierDir / sub;
            fs::create_directories(f, ec);
            std::ofstream o(f / (term + ".txt"));
            for (int id : ids) o << id << "\n";
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double buildSec = std::chrono::duration<double>(t1 - t0).count();

        t0 = std::chrono::high_resolution_clock::now();
        for (const auto& q : querySample) HierarchicalIndex::search(q, hierDir.string());
        t1 = std::chrono::high_resolution_clock::now();
        double lookupMs = (std::chrono::duration<double>(t1 - t0).count() / querySample.size()) * 1000.0;

        int numFiles = 0, numDirs = 0;
        uintmax_t bytes = 0;
        for (const auto& e : fs::recursive_directory_iterator(hierDir, ec)) {
            if (e.is_regular_file()) { numFiles++; bytes += e.file_size(); }
            else if (e.is_directory()) numDirs++;
        }

        std::cout << std::left << std::setw(20) << "hierarchical"
                  << std::setw(12) << std::fixed << std::setprecision(4) << buildSec
                  << std::setw(18) << std::fixed << std::setprecision(4) << lookupMs
                  << std::setw(10) << numFiles << std::setw(10) << numDirs
                  << std::setw(14) << std::fixed << std::setprecision(1) << (bytes / 1024.0) << "\n";

        out["hierarchical"] = {
            {"structure", "hierarchical"}, {"n_terms", fullMap.size()},
            {"build_seconds", buildSec}, {"avg_lookup_ms", lookupMs},
            {"num_files", numFiles}, {"num_dirs", numDirs}, {"total_size_bytes", bytes}
        };
    }

    // 3. Binary Compact
    {
        fs::path binFile = benchDir / "inverted_index.bin";
        auto t0 = std::chrono::high_resolution_clock::now();
        BinaryIndex::buildFromMap(fullMap, binFile.string());
        auto t1 = std::chrono::high_resolution_clock::now();
        double buildSec = std::chrono::duration<double>(t1 - t0).count();

        t0 = std::chrono::high_resolution_clock::now();
        for (const auto& q : querySample) BinaryIndex::search(q, binFile.string());
        t1 = std::chrono::high_resolution_clock::now();
        double lookupMs = (std::chrono::duration<double>(t1 - t0).count() / querySample.size()) * 1000.0;
        double kb = fs::file_size(binFile) / 1024.0;

        std::cout << std::left << std::setw(20) << "binary_compact"
                  << std::setw(12) << std::fixed << std::setprecision(4) << buildSec
                  << std::setw(18) << std::fixed << std::setprecision(4) << lookupMs
                  << std::setw(10) << 1 << std::setw(10) << 0
                  << std::setw(14) << std::fixed << std::setprecision(1) << kb << "\n";

        out["binary_compact"] = {
            {"structure", "binary_compact"}, {"n_terms", fullMap.size()},
            {"build_seconds", buildSec}, {"avg_lookup_ms", lookupMs},
            {"num_files", 1}, {"num_dirs", 0}, {"total_size_bytes", fs::file_size(binFile)}
        };
    }

    fs::remove_all(benchDir, ec);

    if (!outJsonPath.empty()) {
        fs::path p(outJsonPath);
        if (p.has_parent_path()) {
            std::error_code err;
            fs::create_directories(p.parent_path(), err);
        }
        std::ofstream f(outJsonPath);
        f << out.dump(2) << "\n";
        std::cout << "\nResults saved to: " << outJsonPath << "\n";
    }
}
