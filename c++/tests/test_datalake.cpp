#include "test_framework.hpp"
#include "Datalake.hpp"
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

TEST_CASE(Datalake_PathGeneration) {
    fs::path base = "/data/lake";

    fs::path pBook = Datalake::getDirectoryPath(base, DatalakeLayout::BookBased, 1342);
    CHECK_EQ(pBook.string(), (base / "1342").string());

    fs::path pBatch = Datalake::getDirectoryPath(base, DatalakeLayout::BatchBased, 1342);
    CHECK_EQ(pBatch.string(), (base / "batch_2").string()); // 1342 / 500 = 2

    fs::path pTime = Datalake::getDirectoryPath(base, DatalakeLayout::TimeBased, 1342);
    CHECK(pTime.string().find(base.string()) == 0);
}

TEST_CASE(Datalake_SaveAndLocate) {
    fs::path testLake = fs::temp_directory_path() / "test_datalake_storage";
    std::error_code ec;
    fs::remove_all(testLake, ec);

    for (auto layout : {DatalakeLayout::TimeBased, DatalakeLayout::BookBased, DatalakeLayout::BatchBased}) {
        fs::path targetDir = testLake / Datalake::layoutToString(layout);
        bool saved = Datalake::saveBook(targetDir, layout, 42, "Header 42", "Body 42");
        CHECK(saved);

        BookFiles bf = Datalake::locateBook(targetDir, layout, 42);
        CHECK(bf.exists);
        CHECK(fs::exists(bf.headerPath));
        CHECK(fs::exists(bf.bodyPath));

        BookFiles missing = Datalake::locateBook(targetDir, layout, 9999);
        CHECK(!missing.exists);
    }

    fs::remove_all(testLake, ec);
}

TEST_CASE(Datalake_IncrementalAndRecovery) {
    fs::path testLake = fs::temp_directory_path() / "test_datalake_incremental";
    std::error_code ec;
    fs::remove_all(testLake, ec);

    auto layout = DatalakeLayout::BatchBased;
    // Ingest books 1 to 10
    for (int id = 1; id <= 10; ++id) {
        Datalake::saveBook(testLake, layout, id, "H" + std::to_string(id), "B" + std::to_string(id));
    }

    // Incremental detection: candidate IDs 5 to 15 (5..10 exist, 11..15 are new)
    std::vector<int> candidates;
    for (int id = 5; id <= 15; ++id) candidates.push_back(id);

    auto newBooks = Datalake::detectNewBooks(testLake, layout, candidates);
    CHECK_EQ(newBooks.size(), 5);
    for (int id : newBooks) {
        CHECK(id >= 11 && id <= 15);
    }

    // Simulate partial failure / deletion of books 8, 9, 10
    for (int id = 8; id <= 10; ++id) {
        BookFiles bf = Datalake::locateBook(testLake, layout, id);
        if (bf.exists) { fs::remove(bf.headerPath, ec); fs::remove(bf.bodyPath, ec); }
    }

    std::vector<int> expected;
    std::map<int, std::pair<std::string, std::string>> fallback;
    for (int id = 1; id <= 10; ++id) {
        expected.push_back(id);
        fallback[id] = {"H" + std::to_string(id), "B" + std::to_string(id)};
    }

    int restored = Datalake::recoverDatalake(testLake, layout, expected, fallback);
    CHECK_EQ(restored, 3); // 8, 9, 10 restored

    // Verify all 10 exist now
    for (int id = 1; id <= 10; ++id) {
        BookFiles bf = Datalake::locateBook(testLake, layout, id);
        CHECK(bf.exists);
    }

    fs::remove_all(testLake, ec);
}
