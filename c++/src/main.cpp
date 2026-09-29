#include "ControlLayer.hpp"
#include "Datalake.hpp"
#include "MetadataExtractor.hpp"
#include "InvertedIndex.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;

static void printUsage(const char* prog) {
    std::cout << "\n=================================================================\n"
              << " Stage 1 — C++ Big Data Search Engine (TheScratchers)\n"
              << "=================================================================\n"
              << "Usage: " << prog << " <command> [options]\n\n"
              << "Commands:\n"
              << "  step [N]                     Run N pipeline steps (default: 1)\n"
              << "  query <term> [struct]        Search terms in inverted index\n"
              << "                               struct: all (default), json, hier, bin\n"
              << "  query-and <t1> <t2> ...      Boolean AND search across multiple terms\n"
              << "  query-or  <t1> <t2> ...      Boolean OR search across multiple terms\n"
              << "  metadata --id <ID>           Query book metadata by book ID\n"
              << "  metadata --author <name>     Query books by author substring\n"
              << "  metadata --title <title>     Query books by title substring\n"
              << "  metadata --all               List all books in metadata database\n"
              << "  bench-datalake [N]           Benchmark 3 Datalake structures (default N=300)\n"
              << "  bench-index [N]              Benchmark 3 Inverted Index structures\n"
              << "  bench-all                    Run all benchmarks and export JSON metrics\n"
              << "  test-sample                  Run end-to-end test on offline sample dataset\n"
              << "  help                         Display this help message\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 0;
    }

    std::string cmd = argv[1];
    ControlLayer orchestrator;
    fs::path datamartPath = orchestrator.getDatamartPath();
    fs::path datalakePath = orchestrator.getDatalakePath();

    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printUsage(argv[0]);
        return 0;
    }

    // ── Pipeline Step(s) ─────────────────────────────────────────────────────
    if (cmd == "step" || cmd == "run") {
        int steps = 1;
        if (argc > 2) {
            try { steps = std::stoi(argv[2]); } catch (...) { steps = 1; }
        }
        std::cout << ">>> Running " << steps << " pipeline step(s)...\n";
        orchestrator.run(steps);
        std::cout << ">>> Finished pipeline execution.\n";
        return 0;
    }

    // ── Inverted Index Query ─────────────────────────────────────────────────
    if (cmd == "query") {
        if (argc < 3) {
            std::cerr << "Error: Missing search term. Usage: " << argv[0] << " query <term> [json|hier|bin|all]\n";
            return 1;
        }
        std::string term = argv[2];
        std::string structType = (argc > 3) ? argv[3] : "all";

        std::cout << "\n=== Inverted Index Query: \"" << term << "\" ===\n";

        fs::path jsonPath = datamartPath / "inverted_index.json";
        fs::path hierPath = datamartPath / "inverted_index_hier";
        fs::path binPath  = datamartPath / "inverted_index.bin";

        if (structType == "json" || structType == "all") {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto res = InvertedIndex::searchMonolithic(term, jsonPath.string());
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double>(t1 - t0).count() * 1000.0;
            std::cout << "[JSON Monolithic]    Found " << res.size() << " match(es) in " 
                      << std::fixed << std::setprecision(3) << ms << " ms. IDs: [";
            for (size_t i = 0; i < res.size(); ++i) {
                std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
            }
            std::cout << "]\n";
        }

        if (structType == "hier" || structType == "all") {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto res = InvertedIndex::searchHierarchical(term, hierPath.string());
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double>(t1 - t0).count() * 1000.0;
            std::cout << "[Hierarchical Folder] Found " << res.size() << " match(es) in " 
                      << std::fixed << std::setprecision(3) << ms << " ms. IDs: [";
            for (size_t i = 0; i < res.size(); ++i) {
                std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
            }
            std::cout << "]\n";
        }

        if (structType == "bin" || structType == "all") {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto res = InvertedIndex::searchBinary(term, binPath.string());
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double>(t1 - t0).count() * 1000.0;
            std::cout << "[Binary Compact]     Found " << res.size() << " match(es) in " 
                      << std::fixed << std::setprecision(3) << ms << " ms. IDs: [";
            for (size_t i = 0; i < res.size(); ++i) {
                std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
            }
            std::cout << "]\n";
        }
        std::cout << "\n";
        return 0;
    }

    // ── Boolean AND Query ────────────────────────────────────────────────────
    if (cmd == "query-and") {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " query-and <term1> <term2> ...\n";
            return 1;
        }
        std::vector<std::string> terms;
        for (int i = 2; i < argc; ++i) terms.push_back(argv[i]);

        fs::path binPath = datamartPath / "inverted_index.bin";
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = InvertedIndex::queryAnd(terms, IndexStructureType::BinaryCompact, binPath.string());
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double>(t1 - t0).count() * 1000.0;

        std::cout << "Boolean AND Results (" << std::fixed << std::setprecision(3) << ms << " ms): [";
        for (size_t i = 0; i < res.size(); ++i) {
            std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
        }
        std::cout << "]\n";
        return 0;
    }

    // ── Boolean OR Query ─────────────────────────────────────────────────────
    if (cmd == "query-or") {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " query-or <term1> <term2> ...\n";
            return 1;
        }
        std::vector<std::string> terms;
        for (int i = 2; i < argc; ++i) terms.push_back(argv[i]);

        fs::path binPath = datamartPath / "inverted_index.bin";
        auto t0 = std::chrono::high_resolution_clock::now();
        auto res = InvertedIndex::queryOr(terms, IndexStructureType::BinaryCompact, binPath.string());
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double>(t1 - t0).count() * 1000.0;

        std::cout << "Boolean OR Results (" << std::fixed << std::setprecision(3) << ms << " ms): [";
        for (size_t i = 0; i < res.size(); ++i) {
            std::cout << res[i] << (i + 1 < res.size() ? ", " : "");
        }
        std::cout << "]\n";
        return 0;
    }

    // ── Metadata Queries ─────────────────────────────────────────────────────
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

        if (sub == "--author" && argc > 3) {
            std::string author = argv[3];
            auto list = MetadataExtractor::queryByAuthor(author, dbPath.string());
            std::cout << "\nBooks by author matching \"" << author << "\" (" << list.size() << " found):\n";
            for (const auto& b : list) {
                std::cout << "  [" << b.bookId << "] \"" << b.title << "\" by " << b.author << " (" << b.language << ")\n";
            }
            std::cout << "\n";
            return 0;
        }

        if (sub == "--title" && argc > 3) {
            std::string title = argv[3];
            auto list = MetadataExtractor::queryByTitle(title, dbPath.string());
            std::cout << "\nBooks with title matching \"" << title << "\" (" << list.size() << " found):\n";
            for (const auto& b : list) {
                std::cout << "  [" << b.bookId << "] \"" << b.title << "\" by " << b.author << " (" << b.language << ")\n";
            }
            std::cout << "\n";
            return 0;
        }

        if (sub == "--all") {
            auto list = MetadataExtractor::getAllBooks(dbPath.string());
            std::cout << "\nAll books in metadata database (" << list.size() << " total):\n";
            for (const auto& b : list) {
                std::cout << "  [" << b.bookId << "] \"" << b.title << "\" by " << b.author << " (" << b.language << ")\n";
            }
            std::cout << "\n";
            return 0;
        }
    }

    // ── Benchmarks ───────────────────────────────────────────────────────────
    if (cmd == "bench-datalake" || cmd == "bench-all") {
        int nBooks = 200;
        if (argc > 2 && cmd == "bench-datalake") {
            try { nBooks = std::stoi(argv[2]); } catch (...) {}
        }

        std::cout << "\n=================================================================\n"
                  << " Running Datalake Benchmark (" << nBooks << " synthetic books)\n"
                  << "=================================================================\n";

        // Read sample books to replicate genuine vocabulary
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

        // If no datalake books exist yet, use built-in synthetic content
        if (sampleData.empty()) {
            sampleData[1342] = {
                "Title: Pride and Prejudice\nAuthor: Jane Austen\nLanguage: English\n",
                "It is a truth universally acknowledged that a single man in possession of a good fortune must be in want of a wife."
            };
            sampleData[11] = {
                "Title: Alice's Adventures in Wonderland\nAuthor: Lewis Carroll\nLanguage: English\n",
                "Alice was beginning to get very tired of sitting by her sister on the bank and having nothing to do."
            };
        }

        fs::path benchDir = orchestrator.getControlPath().parent_path() / "data" / "bench_datalake";
        fs::path outJson = datamartPath / "benchmark_datalake_results.json";

        auto results = Datalake::runBenchmark(benchDir, sampleData, nBooks, 100, outJson.string());

        std::cout << "\n" << std::left
                  << std::setw(15) << "Structure"
                  << std::setw(12) << "Write (s)"
                  << std::setw(12) << "Books/sec"
                  << std::setw(16) << "Lookup avg(ms)"
                  << std::setw(10) << "# Dirs"
                  << std::setw(12) << "Max Depth"
                  << std::setw(14) << "Avg Files/Dir\n";
        std::cout << std::string(81, '-') << "\n";

        for (const auto& r : results) {
            std::cout << std::left
                      << std::setw(15) << r.layoutName
                      << std::setw(12) << std::fixed << std::setprecision(4) << r.writeSeconds
                      << std::setw(12) << std::fixed << std::setprecision(1) << r.writeBooksPerSec
                      << std::setw(16) << std::fixed << std::setprecision(4) << r.lookupAvgMs
                      << std::setw(10) << r.numDirsCreated
                      << std::setw(12) << r.maxDepth
                      << std::setw(14) << std::fixed << std::setprecision(1) << r.avgFilesPerDir << "\n";
        }
        std::cout << "\nResults saved to: " << outJson << "\n";
        if (cmd == "bench-datalake") return 0;
    }

    if (cmd == "bench-index" || cmd == "bench-all") {
        int nBooks = 100;
        if (argc > 2 && cmd == "bench-index") {
            try { nBooks = std::stoi(argv[2]); } catch (...) {}
        }

        std::cout << "\n=================================================================\n"
                  << " Running Inverted Index Benchmark (3 Structures)\n"
                  << "=================================================================\n";

        std::map<int, std::string> bodies;
        int realIds[] = {5, 11, 84, 1342};
        std::vector<std::string> realTexts;
        for (int id : realIds) {
            BookFiles f = Datalake::findBookRecursive(datalakePath, id);
            if (f.exists) {
                std::ifstream bs(f.bodyPath);
                realTexts.push_back(std::string((std::istreambuf_iterator<char>(bs)), std::istreambuf_iterator<char>()));
            }
        }
        if (realTexts.empty()) {
            realTexts.push_back("The quick brown fox jumps over the lazy dog in the sunny park.");
            realTexts.push_back("It is a truth universally acknowledged that a single man in possession of fortune must want a wife.");
            realTexts.push_back("Alice was beginning to get very tired of sitting by her sister on the river bank.");
        }

        for (int i = 0; i < nBooks; ++i) {
            bodies[200000 + i] = realTexts[i % realTexts.size()];
        }

        std::vector<std::string> queries = {"truth", "elizabeth", "alice", "rabbit", "sister", "fortune", "wife", "adventure", "man", "brown"};

        fs::path benchDir = orchestrator.getControlPath().parent_path() / "data" / "bench_index";
        fs::path outJson = datamartPath / "benchmark_inverted_index_results.json";

        auto results = InvertedIndex::runBenchmark(benchDir, bodies, queries, outJson.string());

        std::cout << "\n" << std::left
                  << std::setw(20) << "Structure"
                  << std::setw(12) << "Build (s)"
                  << std::setw(18) << "Avg Lookup (ms)"
                  << std::setw(10) << "Files"
                  << std::setw(10) << "Dirs"
                  << std::setw(14) << "Size (KB)\n";
        std::cout << std::string(84, '-') << "\n";

        for (const auto& r : results) {
            std::cout << std::left
                      << std::setw(20) << r.structureName
                      << std::setw(12) << std::fixed << std::setprecision(4) << r.buildSeconds
                      << std::setw(18) << std::fixed << std::setprecision(4) << r.avgLookupMs
                      << std::setw(10) << r.numFiles
                      << std::setw(10) << r.numDirs
                      << std::setw(14) << std::fixed << std::setprecision(1) << (r.totalSizeBytes / 1024.0) << "\n";
        }
        std::cout << "\nResults saved to: " << outJson << "\n";
        return 0;
    }

    // ── Sample Offline Test ──────────────────────────────────────────────────
    if (cmd == "test-sample") {
        std::cout << ">>> Ingesting sample books into pipeline...\n";

        std::string sampleBooksDir = (orchestrator.getControlPath().parent_path() / "data" / "sample_books").string();
        fs::create_directories(sampleBooksDir);

        // Ingest sample book 1342
        orchestrator.ingestSampleBook(
            1342,
            "Title: Pride and Prejudice\nAuthor: Jane Austen\nLanguage: English\n",
            "It is a truth universally acknowledged that a single man in possession of a good fortune must be in want of a wife. However little known the feelings or views of such a man may be on his first entering a neighbourhood."
        );

        // Ingest sample book 11
        orchestrator.ingestSampleBook(
            11,
            "Title: Alice's Adventures in Wonderland\nAuthor: Lewis Carroll\nLanguage: English\n",
            "Alice was beginning to get very tired of sitting by her sister on the bank and of having nothing to do. Once or twice she had peeped into the book her sister was reading but it had no pictures or conversations in it."
        );

        // Ingest sample book 84
        orchestrator.ingestSampleBook(
            84,
            "Title: Frankenstein; Or, The Modern Prometheus\nAuthor: Mary Wollstonecraft Shelley\nLanguage: English\n",
            "You will rejoice to hear that no disaster has accompanied the commencement of an enterprise which you have regarded with such evil forebodings. I arrived here yesterday."
        );

        std::cout << ">>> Running indexing for pending sample books...\n";
        orchestrator.run(3);

        std::cout << "\n>>> Verifying SQLite Metadata...\n";
        auto books = MetadataExtractor::getAllBooks((datamartPath / "metadata.db").string());
        for (const auto& b : books) {
            std::cout << "  Found: [" << b.bookId << "] \"" << b.title << "\" by " << b.author << "\n";
        }

        std::cout << "\n>>> Verifying Inverted Index search for 'sister' across structures...\n";
        auto rJson = InvertedIndex::searchMonolithic("sister", (datamartPath / "inverted_index.json").string());
        auto rHier = InvertedIndex::searchHierarchical("sister", (datamartPath / "inverted_index_hier").string());
        auto rBin  = InvertedIndex::searchBinary("sister", (datamartPath / "inverted_index.bin").string());

        std::cout << "  JSON matches: " << rJson.size() << " | Hierarchical matches: " << rHier.size() 
                  << " | Binary matches: " << rBin.size() << "\n";
        std::cout << ">>> Sample test complete!\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << ". Run '" << argv[0] << " help' for usage.\n";
    return 1;
}
