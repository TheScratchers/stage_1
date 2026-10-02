#include "ControlLayer.hpp"
#include "MetadataExtractor.hpp"
#include "QueryEngine.hpp"
#include "BenchmarkRunner.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace fs = std::filesystem;

static void printUsage(const char* p) {
    std::cout << "\n=== Stage 1: C++ Big Data Search Engine ===\n"
              << "Usage: " << p << " <command> [options]\n\n"
              << "Commands:\n"
              << "  step [N]                     Run N pipeline steps (default: 1)\n"
              << "  query <term> [all|json|hier|bin]  Search term in inverted index\n"
              << "  query-and <t1> <t2> ...      Boolean AND search\n"
              << "  query-or  <t1> <t2> ...      Boolean OR search\n"
              << "  metadata [--id N | --author X | --title Y | --all]\n"
              << "  bench-datalake [N]           Benchmark Datalake layouts\n"
              << "  bench-metadata [N]           Benchmark SQLite Metadata storage\n"
              << "  bench-index [N]              Benchmark Inverted Index structures\n"
              << "  bench-all [N]                Run all benchmarks\n"
              << "  test-sample                  Run sample dataset test\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { printUsage(argv[0]); return 0; }
    std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") { printUsage(argv[0]); return 0; }

    ControlLayer ctrl;
    fs::path mPath = ctrl.getDatamartPath();
    fs::path lPath = ctrl.getDatalakePath();

    if (cmd == "step" || cmd == "run") {
        int n = (argc > 2) ? std::max(1, std::stoi(argv[2])) : 1;
        ctrl.run(n);
        return 0;
    }

    if (cmd == "query") {
        if (argc < 3) { std::cerr << "Usage: " << argv[0] << " query <term> [struct]\n"; return 1; }
        std::string term = argv[2], target = (argc > 3) ? argv[3] : "all";
        std::cout << "\n=== Inverted Index Query: \"" << term << "\" ===\n";

        auto search = [&](const char* lbl, IndexType t, const fs::path& p) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto res = QueryEngine::searchSingle(term, t, p.string());
            double ms = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0;
            std::cout << std::left << std::setw(21) << ("[" + std::string(lbl) + "]")
                      << "Found " << res.size() << " match(es) in " << std::fixed << std::setprecision(3) << ms << " ms. IDs: [";
            for (size_t i = 0; i < res.size(); ++i) std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
            std::cout << "]\n";
        };
        if (target == "json" || target == "all") search("JSON Monolithic", IndexType::Json, mPath / "inverted_index.json");
        if (target == "hier" || target == "all") search("Hierarchical", IndexType::Hierarchical, mPath / "inverted_index_hier");
        if (target == "bin"  || target == "all") search("Binary Compact", IndexType::Binary, mPath / "inverted_index.bin");
        std::cout << "\n";
        return 0;
    }

    if (cmd == "query-and" || cmd == "query-or") {
        if (argc < 4) { std::cerr << "Usage: " << argv[0] << " " << cmd << " <t1> <t2> ...\n"; return 1; }
        std::vector<std::string> terms(argv + 2, argv + argc);
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = (cmd == "query-and")
            ? QueryEngine::searchAnd(terms, IndexType::Binary, (mPath / "inverted_index.bin").string())
            : QueryEngine::searchOr(terms, IndexType::Binary, (mPath / "inverted_index.bin").string());
        double ms = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0;

        std::cout << cmd << " Results (" << std::fixed << std::setprecision(3) << ms << " ms): [";
        for (size_t i = 0; i < res.size(); ++i) std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
        std::cout << "]\n";
        return 0;
    }

    if (cmd == "metadata") {
        std::string db = (mPath / "metadata.db").string();
        if (argc < 3) return 1;
        std::string sub = argv[2];
        if (sub == "--id" && argc > 3) {
            auto b = MetadataExtractor::queryById(std::stoi(argv[3]), db);
            if (b) std::cout << "\nBook #" << b->bookId << ": \"" << b->title << "\" by " << b->author << " (" << b->language << ")\n\n";
            else std::cout << "Book not found.\n";
            return 0;
        }
        std::vector<BookMetadata> list;
        if (sub == "--author" && argc > 3) list = MetadataExtractor::queryByAuthor(argv[3], db);
        else if (sub == "--title" && argc > 3) list = MetadataExtractor::queryByTitle(argv[3], db);
        else if (sub == "--all") list = MetadataExtractor::getAllBooks(db);

        std::cout << "\nMatched Books (" << list.size() << "):\n";
        for (const auto& b : list) std::cout << "  [" << b.bookId << "] \"" << b.title << "\" by " << b.author << "\n";
        std::cout << "\n";
        return 0;
    }

    if (cmd == "bench-datalake" || cmd == "bench-index" || cmd == "bench-metadata" || cmd == "bench-all") {
        fs::path bDir = ctrl.getControlPath().parent_path() / "data" / "bench";
        int nParam = (argc > 2) ? std::stoi(argv[2]) : 0;
        int nL = (cmd == "bench-datalake" && nParam > 0) ? nParam : (nParam > 0 ? nParam : 200);
        int nM = (cmd == "bench-metadata" && nParam > 0) ? nParam : (nParam > 0 ? nParam : 1000);
        int nI = (cmd == "bench-index" && nParam > 0) ? nParam : (nParam > 0 ? nParam : 100);

        if (cmd == "bench-datalake" || cmd == "bench-all")
            BenchmarkRunner::runDatalakeBenchmark(bDir / "lake", lPath, nL, (mPath / "benchmark_datalake_results.json").string());
        if (cmd == "bench-metadata" || cmd == "bench-all")
            BenchmarkRunner::runMetadataBenchmark(bDir / "bench_metadata.db", nM, (mPath / "benchmark_metadata_results.json").string());
        if (cmd == "bench-index" || cmd == "bench-all")
            BenchmarkRunner::runIndexBenchmark(bDir / "idx", lPath, nI, (mPath / "benchmark_inverted_index_results.json").string());
        return 0;
    }

    if (cmd == "test-sample") {
        ctrl.ingestSampleBook(1342, "Title: Pride and Prejudice\nAuthor: Jane Austen\nLanguage: English\n",
                              "It is a truth universally acknowledged that a single man in possession of a good fortune must want a wife.");
        ctrl.ingestSampleBook(11, "Title: Alice's Adventures in Wonderland\nAuthor: Lewis Carroll\nLanguage: English\n",
                              "Alice was beginning to get very tired of sitting by her sister on the bank.");
        ctrl.ingestSampleBook(84, "Title: Frankenstein\nAuthor: Mary Shelley\nLanguage: English\n",
                              "You will rejoice to hear that no disaster has accompanied the commencement of an enterprise.");
        ctrl.run(3);

        auto rJson = QueryEngine::searchSingle("sister", IndexType::Json, (mPath / "inverted_index.json").string());
        auto rHier = QueryEngine::searchSingle("sister", IndexType::Hierarchical, (mPath / "inverted_index_hier").string());
        auto rBin  = QueryEngine::searchSingle("sister", IndexType::Binary, (mPath / "inverted_index.bin").string());
        std::cout << "\n>>> Sample test complete! 'sister' matches -> JSON: " << rJson.size()
                  << " | Hierarchical: " << rHier.size() << " | Binary: " << rBin.size() << "\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
