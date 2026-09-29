#include "Tokenizer.hpp"
#include <cctype>

std::string Tokenizer::toLower(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char ch : text) {
        result += static_cast<char>(std::tolower(ch));
    }
    return result;
}

std::set<std::string> Tokenizer::extractUniqueTokens(const std::string& text) {
    std::set<std::string> tokens;
    std::string current;

    for (unsigned char ch : text) {
        if (std::isalpha(ch)) {
            current += static_cast<char>(std::tolower(ch));
        } else if (!current.empty()) {
            tokens.insert(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.insert(current);
    }
    return tokens;
}
