#include "test_framework.hpp"
#include "MetadataExtractor.hpp"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE(Metadata_HeaderParsing) {
    std::string header = 
        "The Project Gutenberg eBook of Frankenstein; Or, The Modern Prometheus\n"
        "Title: Frankenstein; Or, The Modern Prometheus\n"
        "Author: Mary Wollstonecraft Shelley\n"
        "Language: English\n"
        "Release Date: 1993-10-01 [eBook #84]\n";

    BookMetadata meta = MetadataExtractor::parseHeader(84, header);

    CHECK_EQ(meta.bookId, 84);
    CHECK(meta.title.find("Frankenstein") != std::string::npos);
    CHECK(meta.author.find("Mary") != std::string::npos);
    CHECK_EQ(meta.language, "English");
}

TEST_CASE(Metadata_SQLiteCRUD) {
    fs::path tempDb = fs::temp_directory_path() / "test_metadata_crud.db";
    std::error_code ec;
    fs::remove(tempDb, ec);

    BookMetadata b1{11, "Alice's Adventures in Wonderland", "Lewis Carroll", "English", "/path/11.h", "/path/11.b", "1700000000"};
    BookMetadata b2{84, "Frankenstein", "Mary Shelley", "English", "/path/84.h", "/path/84.b", "1700000001"};
    BookMetadata b3{1342, "Pride and Prejudice", "Jane Austen", "English", "/path/1342.h", "/path/1342.b", "1700000002"};

    CHECK(MetadataExtractor::saveToDatabase(b1, tempDb.string()));
    CHECK(MetadataExtractor::saveToDatabase(b2, tempDb.string()));
    CHECK(MetadataExtractor::saveToDatabase(b3, tempDb.string()));

    auto found1 = MetadataExtractor::queryById(11, tempDb.string());
    REQUIRE(found1.has_value());
    CHECK_EQ(found1->title, "Alice's Adventures in Wonderland");
    CHECK_EQ(found1->author, "Lewis Carroll");

    auto austenBooks = MetadataExtractor::queryByAuthor("Austen", tempDb.string());
    CHECK_EQ(austenBooks.size(), 1);
    if (!austenBooks.empty()) {
        CHECK_EQ(austenBooks[0].bookId, 1342);
    }

    auto allBooks = MetadataExtractor::getAllBooks(tempDb.string());
    CHECK_EQ(allBooks.size(), 3);

    fs::remove(tempDb, ec);
}

TEST_CASE(Metadata_BatchInsert) {
    fs::path tempDb = fs::temp_directory_path() / "test_metadata_batch.db";
    std::error_code ec;
    fs::remove(tempDb, ec);

    std::vector<BookMetadata> batch;
    for (int i = 0; i < 50; ++i) {
        batch.push_back({1000 + i, "Book Title #" + std::to_string(i), (i % 2 == 0 ? "Charles Dickens" : "Arthur Conan Doyle"), "en", "/h", "/b", "123"});
    }

    CHECK(MetadataExtractor::saveBatchToDatabase(batch, tempDb.string()));

    auto dickens = MetadataExtractor::queryByAuthor("Charles Dickens", tempDb.string());
    CHECK_EQ(dickens.size(), 25);

    auto all = MetadataExtractor::getAllBooks(tempDb.string());
    CHECK_EQ(all.size(), 50);

    fs::remove(tempDb, ec);
}
