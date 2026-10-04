#pragma once

#include <string>
#include <vector>
#include <set>

// Simple, fast tokenizer that extracts lowercased alphabetic words.
class Tokenizer {
public:
    static std::vector<std::string> tokenize(const std::string& text);
    static std::set<std::string> extractUniqueTokens(const std::string& text);
    static std::string toLower(const std::string& text);
};
