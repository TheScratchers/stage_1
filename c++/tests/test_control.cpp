#include "test_framework.hpp"
#include "ControlLayer.hpp"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE(Control_StateTracking) {
    fs::path tempRoot = fs::temp_directory_path() / "test_control_state";
    std::error_code ec;
    fs::remove_all(tempRoot, ec);

    fs::path ctrlDir = tempRoot / "control";
    fs::path dlFile = ctrlDir / "downloaded_books.txt";
    fs::path idxFile = ctrlDir / "indexed_books.txt";

    fs::create_directories(ctrlDir, ec);

    ControlLayer::appendId(dlFile, 100);
    ControlLayer::appendId(dlFile, 200);
    ControlLayer::appendId(dlFile, 300);

    auto ids = ControlLayer::readIds(dlFile);
    CHECK_EQ(ids.size(), 3);
    CHECK(ids.find(100) != ids.end());
    CHECK(ids.find(200) != ids.end());
    CHECK(ids.find(300) != ids.end());

    ControlLayer::appendId(idxFile, 100);
    auto idxIds = ControlLayer::readIds(idxFile);
    CHECK_EQ(idxIds.size(), 1);
    CHECK(idxIds.find(100) != idxIds.end());

    fs::remove_all(tempRoot, ec);
}

TEST_CASE(Control_PipelineOrchestration) {
    fs::path tempRoot = fs::temp_directory_path() / "test_control_orchestration";
    std::error_code ec;
    fs::remove_all(tempRoot, ec);

    ControlLayer ctrl((tempRoot / "control").string(), (tempRoot / "lake").string(), (tempRoot / "marts").string());

    // Ingest sample book
    bool ingested = ctrl.ingestSampleBook(1342, "Title: Pride and Prejudice\nAuthor: Jane Austen\nLanguage: English\n",
                                          "It is a truth universally acknowledged that a single man in possession of a good fortune must want a wife.");
    CHECK(ingested);

    auto dlIds = ControlLayer::readIds(ctrl.getControlPath() / "downloaded_books.txt");
    CHECK_EQ(dlIds.size(), 1);
    CHECK(dlIds.find(1342) != dlIds.end());

    // Step pipeline to index the downloaded book
    bool stepped = ctrl.step();
    CHECK(stepped);

    auto idxIds = ControlLayer::readIds(ctrl.getControlPath() / "indexed_books.txt");
    CHECK_EQ(idxIds.size(), 1);
    CHECK(idxIds.find(1342) != idxIds.end());

    // Check that SQLite datamart got updated
    auto b = MetadataExtractor::queryById(1342, (ctrl.getDatamartPath() / "metadata.db").string());
    REQUIRE(b.has_value());
    CHECK_EQ(b->title, "Pride and Prejudice");

    fs::remove_all(tempRoot, ec);
}
