#include "ControlLayer.hpp"
#include "MetadataExtractor.hpp"
#include "QueryEngine.hpp"
#include "BenchmarkRunner.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

static void printUsage(const char* prog) {
    std::cout << "\n=================================================================\n"
              << " Stage 1 — C++ Big Data Search Engine (TheScratchers)\n"
              << "=================================================================\n"
              << "Usage: " << prog << " <command> [options]\n\n"
              << "Commands:\n"
              << "  step [N]                     Run N pipeline steps (default: 1)\n"
              << "  query <term> [struct]        Search term in inverted index (all, json, hier, bin)\n"
              << "  query-and <t1> <t2> ...      Boolean AND search across terms\n"
              << "  query-or  <t1> <t2> ...      Boolean OR search across terms\n"
              << "  metadata --id <ID>           Query book metadata by ID\n"
              << "  metadata --author <name>     Query books by author substring\n"
              << "  metadata --title <title>     Query books by title substring\n"
              << "  metadata --all               List all books in SQLite database\n"
              << "  bench-datalake [N]           Benchmark 3 Datalake structures (default: 200)\n"
              << "  bench-index [N]              Benchmark 3 Inverted Index structures (default: 100)\n"
              << "  bench-all                    Run all benchmarks and export JSON metrics\n"
              << "  test-sample                  Run end-to-end test on sample dataset\n"
              << "  help                         Display this help message\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 0;
    }

    std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printUsage(argv[0]);
        return 0;
    }

    ControlLayer orchestrator;
    fs::path datamartPath = orchestrator.getDatamartPath();
    fs::path datalakePath = orchestrator.getDatalakePath();

    // ── Pipeline Step(s) ─────────────────────────────────────────────────────
    if (cmd == "step" || cmd == "run") {
        int steps = (argc > 2) ? std::max(1, std::stoi(argv[2])) : 1;
        std::cout << ">>> Running " << steps << " pipeline step(s)...\n";
        orchestrator.run(steps);
        std::cout << ">>> Finished pipeline execution.\n";
        return 0;
    }

    // ── Single-Term Inverted Index Query ─────────────────────────────────────
    if (cmd == "query") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " query <term> [all|json|hier|bin]\n";
            return 1;
        }
        std::string term = argv[2];
        std::string target = (argc > 3) ? argv[3] : "all";

        std::cout << "\n=== Inverted Index Query: \"" << term << "\" ===\n";

        auto runSearch = [&](const std::string& label, IndexType type, const fs::path& path) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto res = QueryEngine::searchSingle(term, type, path.string());
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double>(t1 - t0).count() * 1000.0;

            std::cout << std::left << std::setw(21) << ("[" + label + "]")
                      << "Found " << res.size() << " match(es) in "
                      << std::fixed << std::setprecision(3) << ms << " ms. IDs: [";
            for (size_t i = 0; i < res.size(); ++i) {
                std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
            }
            std::cout << "]\n";
        };

        if (target == "json" || target == "all")
            runSearch("JSON Monolithic", IndexType::Json, datamartPath / "inverted_index.json");
        if (target == "hier" || target == "all")
            runSearch("Hierarchical Folder", IndexType::Hierarchical, datamartPath / "inverted_index_hier");
        if (target == "bin" || target == "all")
            runSearch("Binary Compact", IndexType::Binary, datamartPath / "inverted_index.bin");

        std::cout << "\n";
        return 0;
    }

    // ── Boolean AND / OR Queries ─────────────────────────────────────────────
    if (cmd == "query-and" || cmd == "query-or") {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " " << cmd << " <term1> <term2> ...\n";
            return 1;
        }
        std::vector<std::string> terms;
        for (int i = 2; i < argc; ++i) terms.push_back(argv[i]);

        fs::path binPath = datamartPath / "inverted_index.bin";
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = (cmd == "query-and")
            ? QueryEngine::searchAnd(terms, IndexType::Binary, binPath.string())
            : QueryEngine::searchOr(terms, IndexType::Binary, binPath.string());
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double>(t1 - t0).count() * 1000.0;

        std::string opLabel = (cmd == "query-and") ? "Boolean AND" : "Boolean OR";
        std::cout << opLabel << " Results (" << std::fixed << std::setprecision(3) << ms << " ms): [";
        for (size_t i = 0; i < res.size(); ++i) {
            std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
        }
        std::cout << "]\n";
        return 0;
    }

    // ── Metadata Queries (SQLite) ────────────────────────────────────────────
    if (cmd == "metadata") {
        fs::path dbPath = datamartPath / "metadata.db";
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " metadata [--id <ID> | --author <name> | --title <title> | --all]\n";
            return 1;
        }
        std::string sub = argv[2];

        if (sub == "--id" && argc > 3) {
            int id = std::stoi(argv[3]);
            auto res = MetadataExtractor::queryById(id, dbPath.string());
            if (res.has_value()) {
                std::cout << "\nFound Book #" << res->bookId << ":\n"
                          << "  Title:       " << res->title << "\n"
                          << "  Author:      " << res->author << "\n"
                          << "  Language:    " << res->language << "\n"
                          << "  Ingested At: " << res->ingestedAt << "\n"
                          << "  Header File: " << res->headerPath << "\n"
                          << "  Body File:   " << res->bodyPath << "\n\n";
            } else {
                std::cout << "No book found with ID " << id << ".\n";
            }
            return 0;
        }

        auto printList = [](const std::string& header, const std::vector<BookMetadata>& list) {
            std::cout << "\n" << header << " (" << list.size() << " found):\n";
            for (const auto& b : list) {
                std::cout << "  [" << b.bookId << "] \"" << b.title << "\" by " << b.author << " (" << b.language << ")\n";
            }
            std::cout << "\n";
        };

        if (sub == "--author" && argc > 3) {
            printList("Books by author matching \"" + std::string(argv[3]) + "\"",
                      MetadataExtractor::queryByAuthor(argv[3], dbPath.string()));
            return 0;
        }
        if (sub == "--title" && argc > 3) {
            printList("Books with title matching \"" + std::string(argv[3]) + "\"",
                      MetadataExtractor::queryByTitle(argv[3], dbPath.string()));
            return 0;
        }
        if (sub == "--all") {
            printList("All books in metadata database",
                      MetadataExtractor::getAllBooks(dbPath.string()));
            return 0;
        }
    }

    // ── Benchmarks ───────────────────────────────────────────────────────────
    if (cmd == "bench-datalake" || cmd == "bench-index" || cmd == "bench-all") {
        fs::path benchDir = orchestrator.getControlPath().parent_path() / "data" / "bench";
        fs::path outLake  = datamartPath / "benchmark_datalake_results.json";
        fs::path outIndex = datamartPath / "benchmark_inverted_index_results.json";

        int nLake = (argc > 2 && cmd == "bench-datalake") ? std::stoi(argv[2]) : 200;
        int nIdx  = (argc > 2 && cmd == "bench-index") ? std::stoi(argv[2]) : 100;

        if (cmd == "bench-datalake" || cmd == "bench-all") {
            BenchmarkRunner::runDatalakeBenchmark(benchDir / "datalake", datalakePath, nLake, outLake.string());
        }
        if (cmd == "bench-index" || cmd == "bench-all") {
            BenchmarkRunner::runIndexBenchmark(benchDir / "index", datalakePath, nIdx, outIndex.string());
        }
        return 0;
    }

    // ── Sample Offline Test ──────────────────────────────────────────────────
    if (cmd == "test-sample") {
        std::cout << ">>> Ingesting sample books into pipeline...\n";

        orchestrator.ingestSampleBook(
            1342,
            "Title: Pride and Prejudice\nAuthor: Jane Austen\nLanguage: English\n",
            "It is a truth universally acknowledged that a single man in possession of a good fortune must be in want of a wife. However little known the feelings or views of such a man may be on his first entering a neighbourhood."
        );
        orchestrator.ingestSampleBook(
            11,
            "Title: Alice's Adventures in Wonderland\nAuthor: Lewis Carroll\nLanguage: English\n",
            "Alice was beginning to get very tired of sitting by her sister on the bank and of having nothing to do. Once or twice she had peeped into the book her sister was reading but it had no pictures or conversations in it."
        );
        orchestrator.ingestSampleBook(
            84,
            "Title: Frankenstein; Or, The Modern Prometheus\nAuthor: Mary Wollstonecraft Shelley\nLanguage: English\n",
            "You will rejoice to hear that no disaster has accompanied the commencement of an enterprise which you have regarded with such evil forebodings. I arrived here yesterday."
        );

        std::cout << ">>> Running indexing for pending sample books...\n";
        orchestrator.run(3);

        std::cout << "\n>>> Verifying SQLite Metadata...\n";
        for (const auto& b : MetadataExtractor::getAllBooks((datamartPath / "metadata.db").string())) {
            std::cout << "  Found: [" << b.bookId << "] \"" << b.title << "\" by " << b.author << "\n";
        }

        std::cout << "\n>>> Verifying Inverted Index search for 'sister' across structures...\n";
        auto rJson = QueryEngine::searchSingle("sister", IndexType::Json, (datamartPath / "inverted_index.json").string());
        auto rHier = QueryEngine::searchSingle("sister", IndexType::Hierarchical, (datamartPath / "inverted_index_hier").string());
        auto rBin  = QueryEngine::searchSingle("sister", IndexType::Binary, (datamartPath / "inverted_index.bin").string());

        std::cout << "  JSON matches: " << rJson.size()
                  << " | Hierarchical matches: " << rHier.size()
                  << " | Binary matches: " << rBin.size() << "\n";
        std::cout << ">>> Sample test complete!\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << ". Run '" << argv[0] << " help' for usage.\n";
    return 1;
}
