#include "preprocessing.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace newsscope {

Preprocessor::Preprocessor() {
    initialize_stop_words();
}

void Preprocessor::initialize_stop_words() {
    // Common English stop words
    stop_words = {
        "a", "an", "and", "are", "as", "at", "be", "by", "for", "from",
        "has", "he", "in", "is", "it", "its", "of", "on", "that", "the",
        "to", "was", "will", "with", "i", "me", "my", "we", "you", "your",
        "or", "but", "if", "not", "can", "could", "would", "should", "may",
        "might", "must", "shall", "do", "does", "did", "have", "had", "been",
        "being", "so", "such", "no", "nor", "only", "own", "same", "then",
        "than", "too", "very", "just", "what", "which", "who", "when", "where",
        "why", "how", "all", "each", "every", "both", "few", "more", "most",
        "other", "some", "any", "having"
    };
}

std::vector<std::string> Preprocessor::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string word;
    
    while (iss >> word) {
        std::string normalized = normalize_token(word);
        if (!normalized.empty()) {
            tokens.push_back(normalized);
        }
    }
    
    return tokens;
}

std::string Preprocessor::normalize_token(const std::string& token) {
    std::string normalized;
    normalized.reserve(token.size());
    
    for (char c : token) {
        // Keep only alphanumeric characters
        if (std::isalnum(static_cast<unsigned char>(c))) {
            normalized += std::tolower(static_cast<unsigned char>(c));
        }
    }
    
    return normalized;
}

void Preprocessor::remove_stop_words(std::vector<std::string>& tokens) {
    auto it = std::remove_if(tokens.begin(), tokens.end(),
                             [this](const std::string& token) {
                                 return is_stop_word(token);
                             });
    tokens.erase(it, tokens.end());
}

bool Preprocessor::is_stop_word(const std::string& word) const {
    return stop_words.find(word) != stop_words.end();
}

std::vector<std::string> Preprocessor::process(const Article& article) {
    // Combine headline and body
    std::string full_text = article.headline + " " + article.body;
    
    // Tokenize
    std::vector<std::string> tokens = tokenize(full_text);
    
    // Remove stop words
    remove_stop_words(tokens);
    
    return tokens;
}

void Preprocessor::add_stop_word(const std::string& word) {
    stop_words.insert(word);
}

void Preprocessor::clear_stop_words() {
    stop_words.clear();
}

} // namespace newsscope
