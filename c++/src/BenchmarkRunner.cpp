#include "BenchmarkRunner.hpp"
#include "Datalake.hpp"
#include "JsonIndex.hpp"
#include "HierarchicalIndex.hpp"
#include "SqliteIndex.hpp"
#include "MetadataExtractor.hpp"
#include "Tokenizer.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/wait.h>
#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <cstdio>
#endif
namespace fs = std::filesystem;
using json = nlohmann::json;

static std::vector<std::pair<std::string, std::string>> loadSampleBooks(const fs::path& p) {
    std::vector<std::pair<std::string, std::string>> books;
    fs::path sDir = p.parent_path() / "sample_books";
    const std::vector<int> ids = {11, 84, 1342, 2701, 1661, 98, 345, 76, 74, 43, 174, 46, 120, 1260, 2554, 844, 1400, 2591, 5200, 55};
    for (int id : ids) {
        fs::path hFile = sDir / (std::to_string(id) + ".header.txt");
        fs::path bFile = sDir / (std::to_string(id) + ".body.txt");
        if (!fs::exists(bFile)) {
            BookFiles bf = Datalake::findBookRecursive(p, id);
            if (bf.exists) { hFile = bf.headerPath; bFile = bf.bodyPath; }
        }
        std::string hText, bText;
        if (fs::exists(hFile)) {
            std::ifstream hs(hFile, std::ios::binary);
            hText.assign((std::istreambuf_iterator<char>(hs)), std::istreambuf_iterator<char>());
        }
        if (fs::exists(bFile)) {
            std::ifstream bs(bFile, std::ios::binary);
            bText.assign((std::istreambuf_iterator<char>(bs)), std::istreambuf_iterator<char>());
        }
        if (!bText.empty()) {
            books.emplace_back(hText.empty() ? ("Title: Book " + std::to_string(id) + "\nAuthor: Author\nLanguage: en\n") : hText, bText);
        }
    }
    return books;
}

static double getProcessMemoryKb() {
#if defined(__APPLE__) && defined(__MACH__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return static_cast<double>(info.resident_size) / 1024.0;
    }
    struct rusage u;
    if (getrusage(RUSAGE_SELF, &u) == 0) return static_cast<double>(u.ru_maxrss) / 1024.0;
    return 0.0;
#elif defined(__linux__)
    long rss = 0L;
    FILE* fp = fopen("/proc/self/statm", "r");
    if (fp) {
        if (fscanf(fp, "%*s%ld", &rss) == 1) rss *= sysconf(_SC_PAGESIZE);
        fclose(fp);
        return static_cast<double>(rss) / 1024.0;
    }
    struct rusage u;
    if (getrusage(RUSAGE_SELF, &u) == 0) return static_cast<double>(u.ru_maxrss);
    return 0.0;
#else
    struct rusage u;
    if (getrusage(RUSAGE_SELF, &u) == 0) return static_cast<double>(u.ru_maxrss);
    return 0.0;
#endif
}

static void saveResultsJson(const std::string& outJson, const json& j) {
    if (outJson.empty()) return;
    fs::path p(outJson);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p) << j.dump(2) << "\n";
    std::cout << "\nResults saved to: " << p.string() << "\n";

    fs::path root = p;
    while (root.has_parent_path() && !fs::exists(root / ".git") && !fs::exists(root / "stage_1_building_data_layer.pdf")) {
        root = root.parent_path();
    }
    if (fs::exists(root / "c++")) {
        fs::path mirrorPath = root / "c++" / "datamarts" / p.filename();
        fs::create_directories(mirrorPath.parent_path(), ec);
        std::ofstream(mirrorPath) << j.dump(2) << "\n";
        std::cout << "Results mirrored to repository: " << mirrorPath.string() << "\n";
    }
}

void BenchmarkRunner::runDatalakeBenchmark(const fs::path& bDir, const fs::path& lPath, const std::vector<int>& scales, const std::string& outJson) {
    auto sample = loadSampleBooks(lPath);
    if (sample.empty()) sample.push_back({"Header", "Sample book body text for benchmarking."});
    json jOut;
    jOut["scales"] = scales;
    jOut["results_by_scale"] = json::object();

    for (int n : scales) {
        std::cout << "\n=== Datalake Benchmark (" << n << " books) ===\n"
                  << std::left << std::setw(14) << "Structure" << std::setw(11) << "Write (s)"
                  << std::setw(12) << "Books/s" << std::setw(14) << "Lookup (ms)"
                  << std::setw(14) << "Incr Det(s)" << std::setw(14) << "Incr Wrt(s)"
                  << std::setw(14) << "Recovery(s)" << std::setw(10) << "Dirs\n"
                  << std::string(103, '-') << "\n";

        json scaleArr = json::array();
        for (auto layout : {DatalakeLayout::TimeBased, DatalakeLayout::BookBased, DatalakeLayout::BatchBased}) {
            std::string name = Datalake::layoutToString(layout);
            fs::path target = bDir / name;
            std::error_code ec;
            fs::remove_all(target, ec);

            // 1. Write throughput
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < n; ++i) {
                const auto& item = sample[i % sample.size()];
                Datalake::saveBook(target, layout, 100000 + i, item.first, item.second);
            }
            double wSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();

            // 2. Lookup latency
            int nLookup = std::min(n, 200);
            t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < nLookup; ++i) {
                Datalake::locateBook(target, layout, 100000 + (i * 17) % n);
            }
            double lMs = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / nLookup) * 1000.0;

            // 3. Storage overhead
            int dirs = 0, maxDepth = 0;
            std::map<fs::path, int> filesPerDir;
            for (const auto& e : fs::recursive_directory_iterator(target, ec)) {
                if (e.is_directory()) {
                    dirs++;
                    int depth = 0;
                    for (auto it = e.path().begin(); it != e.path().end(); ++it) depth++;
                    maxDepth = std::max(maxDepth, depth);
                } else if (e.path().filename().string().find(".body.txt") != std::string::npos) {
                    filesPerDir[e.path().parent_path()]++;
                }
            }
            double avgFiles = filesPerDir.empty() ? 0.0 : static_cast<double>(n) / filesPerDir.size();

            // 4. Incremental detection & writing (50% existing + 10% new)
            int incrNewCount = std::max(1, n / 10);
            std::vector<int> candidates;
            for (int i = n / 2; i < n; ++i) candidates.push_back(100000 + i);
            for (int i = 0; i < incrNewCount; ++i) candidates.push_back(100000 + n + i);

            t0 = std::chrono::high_resolution_clock::now();
            auto newFound = Datalake::detectNewBooks(target, layout, candidates);
            double incrDetSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();

            t0 = std::chrono::high_resolution_clock::now();
            for (int id : newFound) {
                const auto& item = sample[id % sample.size()];
                Datalake::saveBook(target, layout, id, item.first, item.second);
            }
            double incrWrtSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();

            // 5. Recovery behavior (simulate 10% loss and recover idempotently)
            int missingCount = std::max(1, n / 10);
            for (int i = n - missingCount; i < n; ++i) {
                BookFiles bf = Datalake::locateBook(target, layout, 100000 + i);
                if (bf.exists) { fs::remove(bf.headerPath, ec); fs::remove(bf.bodyPath, ec); }
            }
            std::vector<int> expected;
            std::map<int, std::pair<std::string, std::string>> fallback;
            for (int i = 0; i < n; ++i) {
                expected.push_back(100000 + i);
                fallback[100000 + i] = sample[i % sample.size()];
            }
            t0 = std::chrono::high_resolution_clock::now();
            int recovered = Datalake::recoverDatalake(target, layout, expected, fallback);
            double recSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();

            std::cout << std::left << std::setw(14) << name
                      << std::setw(11) << std::fixed << std::setprecision(3) << wSec
                      << std::setw(12) << (n / wSec)
                      << std::setw(14) << lMs
                      << std::setw(14) << incrDetSec
                      << std::setw(14) << incrWrtSec
                      << std::setw(14) << recSec
                      << std::setw(10) << dirs << "\n";

            scaleArr.push_back({
                {"structure", name}, {"n_books", n}, {"write_seconds", wSec}, {"write_books_per_sec", n / wSec},
                {"lookup_n", nLookup}, {"lookup_avg_ms", lMs}, {"num_dirs_created", dirs}, {"max_depth", maxDepth},
                {"avg_files_per_dir", avgFiles}, {"incremental_candidates", candidates.size()},
                {"incremental_new_found", newFound.size()}, {"incremental_detect_seconds", incrDetSec},
                {"incremental_write_seconds", incrWrtSec}, {"recovery_resumed_count", recovered},
                {"recovery_seconds", recSec}, {"recovery_ok", recovered == missingCount}
            });

            fs::remove_all(target, ec);
        }
        jOut["results_by_scale"][std::to_string(n)] = scaleArr;
    }

    saveResultsJson(outJson, jOut);
}

struct IndexBenchMetrics {
    double buildSec = 0.0;
    double lookupMs = 0.0;
    double updateMs = 0.0;
    double peakRamKb = 0.0;
    uintmax_t totalSizeBytes = 0;
    int numFiles = 1;
    int numDirs = 0;
    double coldLoadSec = 0.0;
};

void BenchmarkRunner::runIndexBenchmark(const fs::path& bDir, const fs::path& lPath, const std::vector<int>& scales, const std::string& outJson) {
    auto sample = loadSampleBooks(lPath);
    if (sample.empty()) sample.push_back({"Header", "Sample book body text for benchmarking."});
    const std::vector<std::string> queries = {"time", "love", "heart", "nothing", "death", "truth", "fortune", "monster", "wonderland", "darcy"};
    json jOut;
    jOut["scales"] = scales;
    jOut["results_by_scale"] = json::object();

    for (int n : scales) {
        std::cout << "\n=== Inverted Index Benchmark (" << n << " books) ===\n"
                  << std::left << std::setw(18) << "Structure" << std::setw(11) << "Build (s)"
                  << std::setw(16) << "Lookup avg(ms)" << std::setw(15) << "Update (ms)"
                  << std::setw(14) << "RAM (KB)" << std::setw(14) << "Disk (KB)\n"
                  << std::string(88, '-') << "\n";

        std::map<std::string, std::vector<int>> map;
        for (int i = 0; i < n; ++i) {
            for (const auto& w : Tokenizer::extractUniqueTokens(sample[i % sample.size()].second)) {
                map[w].push_back(200000 + i);
            }
        }
        json scaleObj;
        std::error_code ec;
        fs::create_directories(bDir, ec);

        // 1. JSON Monolithic (isolated process: builds, loads into RAM, searches, updates)
        IndexBenchMetrics jRes{};
        {
            int pfd[2];
            if (pipe(pfd) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    close(pfd[0]);
                    fs::path jFile = bDir / "idx.json";
                    auto t0 = std::chrono::high_resolution_clock::now();
                    JsonIndex::save(map, jFile.string());
                    jRes.buildSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();

                    t0 = std::chrono::high_resolution_clock::now();
                    auto jLoaded = JsonIndex::load(jFile.string());
                    jRes.coldLoadSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
                    jRes.peakRamKb = getProcessMemoryKb();

                    t0 = std::chrono::high_resolution_clock::now();
                    for (const auto& q : queries) jLoaded.find(q);
                    jRes.lookupMs = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;

                    t0 = std::chrono::high_resolution_clock::now();
                    JsonIndex::update(999999, "time love wonderland", jFile.string());
                    jRes.updateMs = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0;
                    jRes.totalSizeBytes = fs::file_size(jFile);
                    jRes.numFiles = 1;
                    jRes.numDirs = 0;

                    fs::remove(jFile, ec);
                    (void)write(pfd[1], &jRes, sizeof(jRes));
                    close(pfd[1]);
                    _exit(0);
                } else if (pid > 0) {
                    close(pfd[1]);
                    (void)read(pfd[0], &jRes, sizeof(jRes));
                    close(pfd[0]);
                    int st = 0;
                    waitpid(pid, &st, 0);
                }
            }
        }

        std::cout << std::left << std::setw(18) << "json_monolithic" << std::setw(11) << std::fixed << std::setprecision(3) << jRes.buildSec
                  << std::setw(16) << jRes.lookupMs << std::setw(15) << jRes.updateMs << std::setw(14) << jRes.peakRamKb << std::setw(14) << (jRes.totalSizeBytes / 1024.0) << "\n";

        scaleObj["json_monolithic"] = {
            {"structure", "json_monolithic"}, {"n_terms", map.size()}, {"build_seconds", jRes.buildSec},
            {"peak_memory_kb", jRes.peakRamKb}, {"cold_load_seconds", jRes.coldLoadSec}, {"in_memory_lookup_avg_ms", jRes.lookupMs},
            {"update_seconds", jRes.updateMs / 1000.0}, {"total_size_bytes", jRes.totalSizeBytes}
        };

        // 2. Hierarchical (isolated process: sharded filesystem directory structure)
        IndexBenchMetrics hRes{};
        {
            int pfd[2];
            if (pipe(pfd) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    close(pfd[0]);
                    fs::path hDir = bDir / "hier";
                    fs::remove_all(hDir, ec);
                    fs::create_directories(hDir / "_", ec);
                    for (char c = 'A'; c <= 'Z'; ++c) fs::create_directories(hDir / std::string(1, c), ec);

                    auto t0 = std::chrono::high_resolution_clock::now();
                    int hFiles = 0;
                    for (const auto& [term, ids] : map) {
                        char ini = std::toupper(static_cast<unsigned char>(term[0]));
                        fs::path p = hDir / ((ini >= 'A' && ini <= 'Z') ? std::string(1, ini) : "_") / (term + ".txt");
                        std::ofstream f(p);
                        for (int id : ids) f << id << "\n";
                        hFiles++;
                    }
                    hRes.buildSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
                    hRes.peakRamKb = getProcessMemoryKb();

                    t0 = std::chrono::high_resolution_clock::now();
                    for (const auto& q : queries) HierarchicalIndex::search(q, hDir.string());
                    hRes.lookupMs = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;

                    t0 = std::chrono::high_resolution_clock::now();
                    HierarchicalIndex::update(999999, "time love wonderland", hDir.string());
                    hRes.updateMs = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0;

                    uintmax_t hBytes = 0;
                    for (const auto& sub : fs::directory_iterator(hDir, ec)) {
                        if (sub.is_directory()) {
                            for (const auto& fileEntry : fs::directory_iterator(sub.path(), ec)) {
                                if (fileEntry.is_regular_file()) hBytes += fileEntry.file_size();
                            }
                        }
                    }
                    hRes.totalSizeBytes = hBytes;
                    hRes.numFiles = hFiles;
                    hRes.numDirs = 27;

                    fs::remove_all(hDir, ec);
                    (void)write(pfd[1], &hRes, sizeof(hRes));
                    close(pfd[1]);
                    _exit(0);
                } else if (pid > 0) {
                    close(pfd[1]);
                    (void)read(pfd[0], &hRes, sizeof(hRes));
                    close(pfd[0]);
                    int st = 0;
                    waitpid(pid, &st, 0);
                }
            }
        }

        std::cout << std::left << std::setw(18) << "hierarchical" << std::setw(11) << hRes.buildSec
                  << std::setw(16) << hRes.lookupMs << std::setw(15) << hRes.updateMs << std::setw(14) << hRes.peakRamKb << std::setw(14) << (hRes.totalSizeBytes / 1024.0) << "\n";

        scaleObj["hierarchical"] = {
            {"structure", "hierarchical"}, {"n_terms", map.size()}, {"build_seconds", hRes.buildSec},
            {"peak_memory_kb", hRes.peakRamKb}, {"avg_lookup_ms", hRes.lookupMs}, {"update_seconds", hRes.updateMs / 1000.0},
            {"num_files", hRes.numFiles}, {"num_dirs", hRes.numDirs}, {"total_size_bytes", hRes.totalSizeBytes}
        };

        // 3. SQLite Relational Index (isolated process: internal page cache buffer)
        IndexBenchMetrics sRes{};
        {
            int pfd[2];
            if (pipe(pfd) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    close(pfd[0]);
                    fs::path sFile = bDir / "idx.db";
                    fs::remove(sFile, ec);
                    auto t0 = std::chrono::high_resolution_clock::now();
                    SqliteIndex::buildFromMap(map, sFile.string());
                    sRes.buildSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();
                    sRes.peakRamKb = getProcessMemoryKb();

                    t0 = std::chrono::high_resolution_clock::now();
                    for (const auto& q : queries) SqliteIndex::search(q, sFile.string());
                    sRes.lookupMs = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / queries.size()) * 1000.0;

                    t0 = std::chrono::high_resolution_clock::now();
                    SqliteIndex::update(999999, "time love wonderland", sFile.string());
                    sRes.updateMs = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() * 1000.0;
                    sRes.totalSizeBytes = fs::exists(sFile) ? fs::file_size(sFile) : 0;
                    sRes.numFiles = 1;
                    sRes.numDirs = 0;

                    fs::remove(sFile, ec);
                    (void)write(pfd[1], &sRes, sizeof(sRes));
                    close(pfd[1]);
                    _exit(0);
                } else if (pid > 0) {
                    close(pfd[1]);
                    (void)read(pfd[0], &sRes, sizeof(sRes));
                    close(pfd[0]);
                    int st = 0;
                    waitpid(pid, &st, 0);
                }
            }
        }

        std::cout << std::left << std::setw(18) << "sqlite_index" << std::setw(11) << sRes.buildSec
                  << std::setw(16) << sRes.lookupMs << std::setw(15) << sRes.updateMs << std::setw(14) << sRes.peakRamKb << std::setw(14) << (sRes.totalSizeBytes / 1024.0) << "\n";

        scaleObj["sqlite_index"] = {
            {"structure", "sqlite_index"}, {"n_terms", map.size()}, {"build_seconds", sRes.buildSec},
            {"peak_memory_kb", sRes.peakRamKb}, {"avg_lookup_ms", sRes.lookupMs}, {"update_seconds", sRes.updateMs / 1000.0},
            {"num_files", 1}, {"total_size_bytes", sRes.totalSizeBytes}
        };

        fs::remove_all(bDir, ec);
        jOut["results_by_scale"][std::to_string(n)] = scaleObj;
    }

    saveResultsJson(outJson, jOut);
}

void BenchmarkRunner::runMetadataBenchmark(const fs::path& dbPath, const std::vector<int>& scales, const std::string& outJson) {
    json jOut;
    jOut["scales"] = scales;
    jOut["results"] = json::array();

    std::cout << "\n=== Metadata SQLite Benchmark ===\n"
              << std::left << std::setw(10) << "Scale" << std::setw(12) << "Insert (s)"
              << std::setw(14) << "Rows/sec" << std::setw(15) << "ID avg(ms)"
              << std::setw(18) << "Dickens avg(ms)" << std::setw(18) << "Carroll avg(ms)"
              << std::setw(12) << "Size (KB)\n" << std::string(99, '-') << "\n";

    struct Seed { std::string title, author; };
    const std::vector<Seed> seeds = {
        {"Alice in Wonderland", "Lewis Carroll"}, {"Frankenstein", "Mary Shelley"}, {"Pride and Prejudice", "Jane Austen"},
        {"A Tale of Two Cities", "Charles Dickens"}, {"A Christmas Carol", "Charles Dickens"}, {"Great Expectations", "Charles Dickens"},
        {"Moby-Dick", "Herman Melville"}, {"Sherlock Holmes", "Arthur Conan Doyle"}, {"Dracula", "Bram Stoker"}, {"Dorian Gray", "Oscar Wilde"}
    };

    for (int n : scales) {
        std::error_code ec;
        fs::remove(dbPath, ec);
        std::vector<BookMetadata> batch;
        batch.reserve(n);
        for (int i = 0; i < n; ++i) {
            const auto& s = seeds[i % seeds.size()];
            batch.push_back({100000 + i, s.title + " #" + std::to_string(i), s.author, "en",
                             "/lake/" + std::to_string(100000 + i) + ".h.txt",
                             "/lake/" + std::to_string(100000 + i) + ".b.txt", "1700000000"});
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        MetadataExtractor::saveBatchToDatabase(batch, dbPath.string());
        double insSec = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count();

        int nQ = std::min(n, 200);
        t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < nQ; ++i) MetadataExtractor::queryById(100000 + (i * 17) % n, dbPath.string());
        double idMs = (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t0).count() / nQ) * 1000.0;

        auto timeQuery = [&](const std::string& auth) {
            auto t = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < 50; ++i) MetadataExtractor::queryByAuthor(auth, dbPath.string());
            return (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t).count() / 50.0) * 1000.0;
        };

        double repMs = timeQuery("Charles Dickens"), unqMs = timeQuery("Lewis Carroll");
        uintmax_t dbBytes = fs::exists(dbPath) ? fs::file_size(dbPath) : 0;

        std::cout << std::left << std::setw(10) << n
                  << std::setw(12) << std::fixed << std::setprecision(4) << insSec
                  << std::setw(14) << (n / insSec)
                  << std::setw(15) << idMs
                  << std::setw(18) << repMs
                  << std::setw(18) << unqMs
                  << std::setw(12) << (dbBytes / 1024.0) << "\n";

        jOut["results"].push_back({
            {"n_books", n}, {"insert_seconds", insSec}, {"insert_books_per_sec", n / insSec},
            {"find_by_id_avg_ms", idMs}, {"find_by_author_repeated_avg_ms", repMs},
            {"find_by_author_unique_avg_ms", unqMs}, {"db_size_bytes", dbBytes}
        });
        fs::remove(dbPath, ec);
    }

    saveResultsJson(outJson, jOut);
}
