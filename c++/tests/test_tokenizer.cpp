#include "test_framework.hpp"
#include "Tokenizer.hpp"

TEST_CASE(Tokenizer_BasicExtraction) {
    std::string text = "Hello, World! 123 This IS a test-case; with symbols: #@$%.";
    auto tokens = Tokenizer::tokenize(text);

    CHECK_EQ(tokens.size(), 9);
    if (tokens.size() >= 9) {
        CHECK_EQ(tokens[0], "hello");
        CHECK_EQ(tokens[1], "world");
        CHECK_EQ(tokens[2], "this");
        CHECK_EQ(tokens[3], "is");
        CHECK_EQ(tokens[4], "a");
        CHECK_EQ(tokens[5], "test");
        CHECK_EQ(tokens[6], "case");
        CHECK_EQ(tokens[7], "with");
        CHECK_EQ(tokens[8], "symbols");
    }
}

TEST_CASE(Tokenizer_UniqueTokens) {
    std::string text = "apple Banana APPLE orange banana APPLE";
    auto unique = Tokenizer::extractUniqueTokens(text);

    CHECK_EQ(unique.size(), 3);
    CHECK_EQ(unique.count("apple"), 1);
    CHECK_EQ(unique.count("banana"), 1);
    CHECK_EQ(unique.count("orange"), 1);
}

TEST_CASE(Tokenizer_ContractCompliance) {
    // Shared contract rule: lowercase, [A-Za-z]+, no numbers/punctuation, no stopword stripping
    std::string sample = "Project Gutenberg's Alice's Adventures in Wonderland, by Lewis Carroll (1865)";
    auto tokens = Tokenizer::tokenize(sample);

    CHECK(tokens.size() > 5);
    CHECK_EQ(tokens[0], "project");
    CHECK_EQ(tokens[1], "gutenberg");
    CHECK_EQ(tokens[2], "s");
    CHECK_EQ(tokens[3], "alice");
    CHECK_EQ(tokens[4], "s");
    CHECK_EQ(tokens[5], "adventures");
}
