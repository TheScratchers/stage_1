#include "test_framework.hpp"
#include "JsonIndex.hpp"
#include "HierarchicalIndex.hpp"
#include "SqliteIndex.hpp"
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

TEST_CASE(Indices_JsonIndex) {
    fs::path testFile = fs::temp_directory_path() / "test_idx.json";
    std::error_code ec;
    fs::remove(testFile, ec);

    std::map<std::string, std::vector<int>> map = {
        {"adventure", {5, 12, 42}},
        {"island", {5, 1342}},
        {"shipwreck", {12, 17}}
    };

    CHECK(JsonIndex::save(map, testFile.string()));

    auto loaded = JsonIndex::load(testFile.string());
    CHECK_EQ(loaded.size(), 3);
    CHECK_EQ(loaded["adventure"].size(), 3);

    // Update with new book
    CHECK(JsonIndex::update(99, "new adventure on an island", testFile.string()));
    auto updated = JsonIndex::load(testFile.string());
    CHECK_EQ(updated["adventure"].size(), 4);
    CHECK_EQ(updated["adventure"].back(), 99);

    fs::remove(testFile, ec);
}

TEST_CASE(Indices_HierarchicalIndex) {
    fs::path testDir = fs::temp_directory_path() / "test_idx_hier";
    std::error_code ec;
    fs::remove_all(testDir, ec);

    CHECK(HierarchicalIndex::update(11, "alice wonderland rabbit", testDir.string()));
    CHECK(HierarchicalIndex::update(84, "frankenstein monster science", testDir.string()));
    CHECK(HierarchicalIndex::update(1342, "pride prejudice wonderland", testDir.string()));

    auto rAlice = HierarchicalIndex::search("alice", testDir.string());
    CHECK_EQ(rAlice.size(), 1);
    CHECK_EQ(rAlice[0], 11);

    auto rWonderland = HierarchicalIndex::search("wonderland", testDir.string());
    CHECK_EQ(rWonderland.size(), 2);

    auto rNone = HierarchicalIndex::search("nonexistent", testDir.string());
    CHECK_EQ(rNone.size(), 0);

    fs::remove_all(testDir, ec);
}

TEST_CASE(Indices_SqliteIndex) {
    fs::path testDb = fs::temp_directory_path() / "test_idx.db";
    std::error_code ec;
    fs::remove(testDb, ec);

    std::map<std::string, std::vector<int>> map = {
        {"truth", {1342, 2701}},
        {"fortune", {84, 1342}},
        {"monster", {84, 1661, 2701}}
    };

    CHECK(SqliteIndex::buildFromMap(map, testDb.string()));

    auto rTruth = SqliteIndex::search("truth", testDb.string());
    CHECK_EQ(rTruth.size(), 2);
    CHECK_EQ(rTruth[0], 1342);

    auto rMonster = SqliteIndex::search("monster", testDb.string());
    CHECK_EQ(rMonster.size(), 3);

    // Update sqlite index
    CHECK(SqliteIndex::update(999, "truth and honor", testDb.string()));
    auto rTruthUpdated = SqliteIndex::search("truth", testDb.string());
    CHECK_EQ(rTruthUpdated.size(), 3);

    fs::remove(testDb, ec);
}

TEST_CASE(Indices_ConsistencyAcrossStructures) {
    fs::path tempRoot = fs::temp_directory_path() / "test_idx_consistency";
    std::error_code ec;
    fs::remove_all(tempRoot, ec);
    fs::create_directories(tempRoot, ec);

    fs::path jPath = tempRoot / "idx.json";
    fs::path hPath = tempRoot / "hier";
    fs::path sPath = tempRoot / "idx.db";

    std::map<std::string, std::vector<int>> corpus = {
        {"common", {1, 2, 3, 4, 5}},
        {"rare", {2}},
        {"shared", {3, 5}}
    };

    JsonIndex::save(corpus, jPath.string());
    for (const auto& [t, ids] : corpus) {
        for (int id : ids) HierarchicalIndex::update(id, t, hPath.string());
    }
    SqliteIndex::buildFromMap(corpus, sPath.string());

    for (const std::string& term : {"common", "rare", "shared", "absent"}) {
        auto jRes = JsonIndex::load(jPath.string())[term];
        auto hRes = HierarchicalIndex::search(term, hPath.string());
        auto sRes = SqliteIndex::search(term, sPath.string());

        CHECK_EQ(hRes.size(), sRes.size());
        if (term != "absent") {
            CHECK_EQ(jRes.size(), hRes.size());
        }
    }

    fs::remove_all(tempRoot, ec);
}
