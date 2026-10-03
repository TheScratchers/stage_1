#include "test_framework.hpp"
#include "QueryEngine.hpp"
#include "BinaryIndex.hpp"
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

TEST_CASE(QueryEngine_BooleanSearch) {
    fs::path testBin = fs::temp_directory_path() / "test_qe_idx.bin";
    std::error_code ec;
    fs::remove(testBin, ec);

    std::map<std::string, std::vector<int>> map = {
        {"truth", {10, 20, 30, 40}},
        {"fortune", {20, 40, 50}},
        {"wife", {20, 30, 60}}
    };
    BinaryIndex::buildFromMap(map, testBin.string());

    // Single term
    auto single = QueryEngine::searchSingle("truth", IndexType::Binary, testBin.string());
    CHECK_EQ(single.size(), 4);

    // AND query: "truth" AND "fortune" -> {20, 40}
    auto andRes = QueryEngine::searchAnd({"truth", "fortune"}, IndexType::Binary, testBin.string());
    CHECK_EQ(andRes.size(), 2);
    if (andRes.size() >= 2) {
        CHECK_EQ(andRes[0], 20);
        CHECK_EQ(andRes[1], 40);
    }

    // AND query: "truth" AND "fortune" AND "wife" -> {20}
    auto andRes3 = QueryEngine::searchAnd({"truth", "fortune", "wife"}, IndexType::Binary, testBin.string());
    CHECK_EQ(andRes3.size(), 1);
    if (!andRes3.empty()) {
        CHECK_EQ(andRes3[0], 20);
    }

    // OR query: "fortune" OR "wife" -> {20, 30, 40, 50, 60}
    auto orRes = QueryEngine::searchOr({"fortune", "wife"}, IndexType::Binary, testBin.string());
    CHECK_EQ(orRes.size(), 5);

    fs::remove(testBin, ec);
}
