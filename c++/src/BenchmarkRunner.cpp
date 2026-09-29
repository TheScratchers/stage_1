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
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::vector<std::string> getSampleTexts(const fs::path& datalakePath) {
    std::vector<std::string> texts;
    for (int id : {5, 11, 84, 1342}) {
        BookFiles f = Datalake::findBookRecursive(datalakePath, id);
        if (f.exists) {
            std::ifstream bs(f.bodyPath);
            texts.emplace_back((std::istreambuf_iterator<char>(bs)), std::istreambuf_iterator<char>());
        }
    }
    if (texts.empty()) {
        texts.push_back("It is a truth universally acknowledged that a single man in possession of fortune must want a wife.");
        texts.push_back("Alice was beginning to get very tired of sitting by her sister on the bank.");
    }
    return texts;
}

void BenchmarkRunner::runDatalakeBenchmark(const fs::path& benchDir, const fs::path& datalakePath, int n, const std::string& outJson) {
    std::cout << "\n=== Datalake Benchmark (" << n << " books) ===\n";
    std::cout << std::left << std::setw(15) << "Structure" << std::setw(12) << "Write (s)" << std::setw(12) << "Books/sec"
              << std::setw(16) << "Lookup avg(ms)" << std::setw(10) << "# Dirs\n" << std::string(65, '-') << "\n";

    auto sample = getSampleTexts(datalakePath);
    json jOut = json::array();

    for (auto layout : {DatalakeLayout::TimeBased, DatalakeLayout::BookBased, DatalakeLayout::BatchBased}) {
        std::string name = Datalake::layoutToString(layout);
        fs::path target = benchDir / name;
        std::error_code ec;
        fs::remove_all(target, ec);

        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < n; ++i) {
            Datalake::saveBook(target, layout, 100000 + i, "Header", sample[i % sample.size()]);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double wSec = std::chrono::duration<double>(t1 - t0).count();

        t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < std::min(n, 100); ++i) Datalake::locateBook(target, layout, 100000 + i);
        t1 = std::chrono::high_resolution_clock::now();
        double lMs = (std::chrono::duration<double>(t1 - t0).count() / std::min(n, 100)) * 1000.0;

        int dirs = 0;
        for (const auto& e : fs::recursive_directory_iterator(target, ec)) if (e.is_directory()) dirs++;

        std::cout << std::left << std::setw(15) << name << std::setw(12) << std::fixed << std::setprecision(4) << wSec
                  << std::setw(12) << std::fixed << std::setprecision(1) << (n / wSec)
                  << std::setw(16) << std::fixed << std::setprecision(4) << lMs << std::setw(10) << dirs << "\n";

        jOut.push_back({{"structure", name}, {"write_sec", wSec}, {"books_per_sec", n / wSec}, {"lookup_ms", lMs}, {"dirs", dirs}});
        fs::remove_all(target, ec);
    }

    if (!outJson.empty()) {
        fs::create_directories(fs::path(outJson).parent_path());
        std::ofstream(outJson) << jOut.dump(2) << "\n";
        std::cout << "Results saved to: " << outJson << "\n";
    }
}

void BenchmarkRunner::runIndexBenchmark(const fs::path& benchDir, const fs::path& datalakePath, int n, const std::string& outJson) {
    std::cout << "\n=== Inverted Index Benchmark (3 Structures) ===\n";
    std::cout << std::left << std::setw(20) << "Structure" << std::setw(12) << "Build (s)" << std::setw(18) << "Avg Lookup (ms)" << std::setw(12) << "Size (KB)\n"
              << std::string(62, '-') << "\n";

    auto sample = getSampleTexts(datalakePath);
    std::map<std::string, std::vector<int>> map;
    for (int i = 0; i < n; ++i) {
        for (const auto& w : Tokenizer::extractUniqueTokens(sample[i % sample.size()])) {
            map[w].push_back(200000 + i);
        }
    }
    std::vector<std::string> queries = {"truth", "fortune", "wife", "sister", "alice"};
    json jOut;
    std::error_code ec;
    fs::create_directories(benchDir, ec);

    // 1. JSON
    fs::path jFile = benchDir / "idx.json";
    auto t0 = std::chrono::high_resolution_clock::now();
    JsonIndex::save(map, jFile.string());
    double jBuild = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
    auto jLoaded = JsonIndex::load(jFile.string());
    t0 = std::chrono::high_resolution_clock::now();
    for (const auto& q : queries) jLoaded.find(q);
    double jLookup = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;
    double jKb = fs::file_size(jFile) / 1024.0;
    std::cout << std::left << std::setw(20) << "json_monolithic" << std::setw(12) << jBuild << std::setw(18) << jLookup << std::setw(12) << jKb << "\n";
    jOut["json_monolithic"] = {{"build_sec", jBuild}, {"lookup_ms", jLookup}, {"size_kb", jKb}};

    // 2. Hierarchical
    fs::path hDir = benchDir / "hier";
    fs::remove_all(hDir, ec);
    t0 = std::chrono::high_resolution_clock::now();
    for (const auto& [term, ids] : map) {
        char ini = std::toupper(static_cast<unsigned char>(term[0]));
        fs::path p = hDir / ((ini >= 'A' && ini <= 'Z') ? std::string(1, ini) : "_");
        fs::create_directories(p, ec);
        std::ofstream f(p / (term + ".txt"));
        for (int id : ids) f << id << "\n";
    }
    double hBuild = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
    t0 = std::chrono::high_resolution_clock::now();
    for (const auto& q : queries) HierarchicalIndex::search(q, hDir.string());
    double hLookup = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;
    uintmax_t hBytes = 0;
    for (const auto& e : fs::recursive_directory_iterator(hDir, ec)) if (e.is_regular_file()) hBytes += e.file_size();
    std::cout << std::left << std::setw(20) << "hierarchical" << std::setw(12) << hBuild << std::setw(18) << hLookup << std::setw(12) << (hBytes / 1024.0) << "\n";
    jOut["hierarchical"] = {{"build_sec", hBuild}, {"lookup_ms", hLookup}, {"size_kb", hBytes / 1024.0}};

    // 3. Binary
    fs::path bFile = benchDir / "idx.bin";
    t0 = std::chrono::high_resolution_clock::now();
    BinaryIndex::buildFromMap(map, bFile.string());
    double bBuild = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
    t0 = std::chrono::high_resolution_clock::now();
    for (const auto& q : queries) BinaryIndex::search(q, bFile.string());
    double bLookup = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;
    double bKb = fs::file_size(bFile) / 1024.0;
    std::cout << std::left << std::setw(20) << "binary_compact" << std::setw(12) << bBuild << std::setw(18) << bLookup << std::setw(12) << bKb << "\n";
    jOut["binary_compact"] = {{"build_sec", bBuild}, {"lookup_ms", bLookup}, {"size_kb", bKb}};

    fs::remove_all(benchDir, ec);
    if (!outJson.empty()) {
        fs::create_directories(fs::path(outJson).parent_path());
        std::ofstream(outJson) << jOut.dump(2) << "\n";
        std::cout << "Results saved to: " << outJson << "\n";
    }
}
