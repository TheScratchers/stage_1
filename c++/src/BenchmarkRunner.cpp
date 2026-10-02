#include "BenchmarkRunner.hpp"
#include "Datalake.hpp"
#include "JsonIndex.hpp"
#include "HierarchicalIndex.hpp"
#include "BinaryIndex.hpp"
#include "MetadataExtractor.hpp"
#include "Tokenizer.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::vector<std::string> getSampleTexts(const fs::path& p) {
    std::vector<std::string> texts;
    fs::path sDir = p.parent_path() / "sample_books";
    for (int id : {11, 84, 1342, 2701, 1661, 98, 345, 76, 74, 43, 174, 46, 120, 1260, 2554, 844, 1400, 2591, 5200, 55}) {
        fs::path f = sDir / (std::to_string(id) + ".body.txt");
        if (!fs::exists(f)) { BookFiles bf = Datalake::findBookRecursive(p, id); if (bf.exists) f = bf.bodyPath; }
        if (fs::exists(f)) {
            std::ifstream bs(f, std::ios::binary);
            texts.emplace_back((std::istreambuf_iterator<char>(bs)), std::istreambuf_iterator<char>());
        }
    }
    return texts;
}

void BenchmarkRunner::runDatalakeBenchmark(const fs::path& bDir, const fs::path& lPath, int n, const std::string& outJson) {
    std::cout << "\n=== Datalake Benchmark (" << n << " books) ===\n" << std::left << std::setw(15) << "Structure"
              << std::setw(12) << "Write (s)" << std::setw(12) << "Books/sec" << std::setw(16) << "Lookup avg(ms)" << std::setw(10) << "# Dirs\n" << std::string(65, '-') << "\n";
    auto sample = getSampleTexts(lPath); json jOut = json::array();
    for (auto layout : {DatalakeLayout::TimeBased, DatalakeLayout::BookBased, DatalakeLayout::BatchBased}) {
        std::string name = Datalake::layoutToString(layout); fs::path target = bDir / name; std::error_code ec; fs::remove_all(target, ec);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < n; ++i) Datalake::saveBook(target, layout, 100000 + i, "Header", sample[i % sample.size()]);
        double wSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
        t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < std::min(n, 100); ++i) Datalake::locateBook(target, layout, 100000 + i);
        double lMs = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / std::min(n, 100)) * 1000.0;
        int dirs = 0; for (const auto& e : fs::recursive_directory_iterator(target, ec)) if (e.is_directory()) dirs++;
        std::cout << std::left << std::setw(15) << name << std::setw(12) << std::fixed << std::setprecision(4) << wSec
                  << std::setw(12) << (n / wSec) << std::setw(16) << lMs << std::setw(10) << dirs << "\n";
        jOut.push_back({{"structure", name}, {"write_sec", wSec}, {"books_per_sec", n / wSec}, {"lookup_ms", lMs}, {"dirs", dirs}});
        fs::remove_all(target, ec);
    }
    if (!outJson.empty()) { fs::create_directories(fs::path(outJson).parent_path()); std::ofstream(outJson) << jOut.dump(2) << "\n"; std::cout << "Results saved to: " << outJson << "\n"; }
}

void BenchmarkRunner::runIndexBenchmark(const fs::path& bDir, const fs::path& lPath, int n, const std::string& outJson) {
    std::cout << "\n=== Inverted Index Benchmark (3 Structures) ===\n" << std::left << std::setw(18) << "Structure"
              << std::setw(11) << "Build (s)" << std::setw(16) << "Lookup avg(ms)" << std::setw(15) << "Update (ms)" << std::setw(12) << "Size (KB)\n" << std::string(72, '-') << "\n";
    auto sample = getSampleTexts(lPath); std::map<std::string, std::vector<int>> map;
    for (int i = 0; i < n; ++i) { for (const auto& w : Tokenizer::extractUniqueTokens(sample[i % sample.size()])) map[w].push_back(200000 + i); }
    std::vector<std::string> queries = {"time", "love", "heart", "nothing", "death", "truth", "fortune", "monster", "wonderland", "darcy"};
    json jOut; std::error_code ec; fs::create_directories(bDir, ec);

    // JSON
    fs::path jFile = bDir / "idx.json"; auto t0 = std::chrono::high_resolution_clock::now(); JsonIndex::save(map, jFile.string());
    double jBuild = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count(); auto jLoaded = JsonIndex::load(jFile.string());
    t0 = std::chrono::high_resolution_clock::now(); for (const auto& q : queries) jLoaded.find(q);
    double jLookup = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;
    t0 = std::chrono::high_resolution_clock::now(); JsonIndex::update(999999, "time love wonderland", jFile.string());
    double jUp = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0; double jKb = fs::file_size(jFile) / 1024.0;
    std::cout << std::left << std::setw(18) << "json_monolithic" << std::setw(11) << jBuild << std::setw(16) << jLookup << std::setw(15) << jUp << std::setw(12) << jKb << "\n";
    jOut["json_monolithic"] = {{"build_sec", jBuild}, {"lookup_ms", jLookup}, {"update_ms", jUp}, {"size_kb", jKb}};

    // Hierarchical
    fs::path hDir = bDir / "hier"; fs::remove_all(hDir, ec); t0 = std::chrono::high_resolution_clock::now();
    for (const auto& [term, ids] : map) {
        char ini = std::toupper(static_cast<unsigned char>(term[0])); fs::path p = hDir / ((ini >= 'A' && ini <= 'Z') ? std::string(1, ini) : "_");
        fs::create_directories(p, ec); std::ofstream f(p / (term + ".txt")); for (int id : ids) f << id << "\n";
    }
    double hBuild = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count(); t0 = std::chrono::high_resolution_clock::now();
    for (const auto& q : queries) HierarchicalIndex::search(q, hDir.string());
    double hLookup = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;
    t0 = std::chrono::high_resolution_clock::now(); HierarchicalIndex::update(999999, "time love wonderland", hDir.string());
    double hUp = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0; uintmax_t hBytes = 0;
    for (const auto& e : fs::recursive_directory_iterator(hDir, ec)) if (e.is_regular_file()) hBytes += e.file_size();
    std::cout << std::left << std::setw(18) << "hierarchical" << std::setw(11) << hBuild << std::setw(16) << hLookup << std::setw(15) << hUp << std::setw(12) << (hBytes / 1024.0) << "\n";
    jOut["hierarchical"] = {{"build_sec", hBuild}, {"lookup_ms", hLookup}, {"update_ms", hUp}, {"size_kb", hBytes / 1024.0}};

    // Binary
    fs::path bFile = bDir / "idx.bin"; t0 = std::chrono::high_resolution_clock::now(); BinaryIndex::buildFromMap(map, bFile.string());
    double bBuild = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count(); t0 = std::chrono::high_resolution_clock::now();
    for (const auto& q : queries) BinaryIndex::search(q, bFile.string());
    double bLookup = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;
    t0 = std::chrono::high_resolution_clock::now(); BinaryIndex::update(999999, "time love wonderland", bFile.string());
    double bUp = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0; double bKb = fs::file_size(bFile) / 1024.0;
    std::cout << std::left << std::setw(18) << "binary_compact" << std::setw(11) << bBuild << std::setw(16) << bLookup << std::setw(15) << bUp << std::setw(12) << bKb << "\n";
    jOut["binary_compact"] = {{"build_sec", bBuild}, {"lookup_ms", bLookup}, {"update_ms", bUp}, {"size_kb", bKb}};

    fs::remove_all(bDir, ec);
    if (!outJson.empty()) { fs::create_directories(fs::path(outJson).parent_path()); std::ofstream(outJson) << jOut.dump(2) << "\n"; std::cout << "Results saved to: " << outJson << "\n"; }
}

void BenchmarkRunner::runMetadataBenchmark(const fs::path& dbPath, int n, const std::string& outJson) {
    std::cout << "\n=== Metadata Benchmark (" << n << " books) ===\n"; std::error_code ec; fs::remove(dbPath, ec);
    struct Seed { std::string title, author; };
    const std::vector<Seed> seeds = {
        {"Alice in Wonderland", "Lewis Carroll"}, {"Frankenstein", "Mary Shelley"}, {"Pride and Prejudice", "Jane Austen"},
        {"A Tale of Two Cities", "Charles Dickens"}, {"A Christmas Carol", "Charles Dickens"}, {"Great Expectations", "Charles Dickens"},
        {"Moby-Dick", "Herman Melville"}, {"Sherlock Holmes", "Arthur Conan Doyle"}, {"Dracula", "Bram Stoker"}, {"Dorian Gray", "Oscar Wilde"}
    };
    std::vector<BookMetadata> batch; batch.reserve(n);
    for (int i = 0; i < n; ++i) {
        const auto& s = seeds[i % seeds.size()];
        batch.push_back({100000 + i, s.title + " #" + std::to_string(i), s.author, "en", "/lake/" + std::to_string(100000 + i) + ".h.txt", "/lake/" + std::to_string(100000 + i) + ".b.txt", "1700000000"});
    }
    auto t0 = std::chrono::high_resolution_clock::now(); MetadataExtractor::saveBatchToDatabase(batch, dbPath.string());
    double insSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
    int nQ = std::min(n, 200); t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < nQ; ++i) MetadataExtractor::queryById(100000 + (i * 17) % n, dbPath.string());
    double idMs = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / nQ) * 1000.0;
    auto timeQuery = [&](const std::string& auth) {
        auto t = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 50; ++i) MetadataExtractor::queryByAuthor(auth, dbPath.string());
        return (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t).count() / 50.0) * 1000.0;
    };
    double repMs = timeQuery("Charles Dickens"), unqMs = timeQuery("Lewis Carroll"), dbKb = fs::exists(dbPath) ? (fs::file_size(dbPath) / 1024.0) : 0.0;
    std::cout << std::left << std::setw(14) << "Insert (s)" << std::setw(14) << "Rows/sec" << std::setw(16) << "ID avg(ms)"
              << std::setw(18) << "Dickens avg(ms)" << std::setw(18) << "Carroll avg(ms)" << std::setw(12) << "Size (KB)\n" << std::string(92, '-') << "\n";
    std::cout << std::left << std::setw(14) << std::fixed << std::setprecision(4) << insSec << std::setw(14) << (n / insSec)
              << std::setw(16) << idMs << std::setw(18) << repMs << std::setw(18) << unqMs << std::setw(12) << dbKb << "\n";
    json j = {{"n_books", n}, {"insert_sec", insSec}, {"rows_per_sec", n / insSec},
              {"query_by_id_ms", idMs}, {"query_dickens_ms", repMs}, {"query_carroll_ms", unqMs}, {"size_kb", dbKb}};
    if (!outJson.empty()) { fs::create_directories(fs::path(outJson).parent_path()); std::ofstream(outJson) << j.dump(2) << "\n"; std::cout << "Results saved to: " << outJson << "\n"; }
}
