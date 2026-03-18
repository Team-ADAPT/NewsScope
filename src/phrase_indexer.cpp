#include "phrase_indexer.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace {
std::string normalize_phrase(const std::string& phrase) {
    std::string normalized;
    normalized.reserve(phrase.size());
    for (char c : phrase) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return normalized;
}
}

namespace newsscope {

PhraseIndexer::PhraseIndexer() : root(std::make_unique<TrieNode>()), phrase_count_(0) {}

void PhraseIndexer::insert_phrase(const std::string& phrase) {
    const std::string normalized_phrase = normalize_phrase(phrase);
    TrieNode* node = root.get();
    
    for (char c : normalized_phrase) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 128) continue;  // skip non-ASCII
        if (!node->children[uc]) {
            node->children[uc] = std::make_unique<TrieNode>();
        }
        node = node->children[uc].get();
    }
    
    if (!node->is_end_of_phrase) {
        node->is_end_of_phrase = true;
        node->phrase = normalized_phrase;
        phrase_count_++;
    }
}

bool PhraseIndexer::phrase_exists(const std::string& phrase) const {
    const std::string normalized_phrase = normalize_phrase(phrase);
    const TrieNode* node = root.get();
    
    for (char c : normalized_phrase) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 128 || !node->children[uc]) return false;
        node = node->children[uc].get();
    }
    
    return node->is_end_of_phrase;
}

std::vector<std::string> PhraseIndexer::find_by_prefix(const std::string& prefix) const {
    std::vector<std::string> result;
    const TrieNode* node = root.get();
    const std::string normalized_prefix = normalize_phrase(prefix);
    
    for (char c : normalized_prefix) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 128 || !node->children[uc]) return result;
        node = node->children[uc].get();
    }
    
    dfs_collect_phrases(node, normalized_prefix, result);
    return result;
}

void PhraseIndexer::dfs_collect_phrases(const TrieNode* node,
                                        const std::string& prefix,
                                        std::vector<std::string>& result) const {
    if (node->is_end_of_phrase) {
        result.push_back(node->phrase);
    }
    
    for (int i = 0; i < 128; ++i) {
        if (node->children[i]) {
            dfs_collect_phrases(node->children[i].get(),
                                prefix + static_cast<char>(i), result);
        }
    }
}

std::vector<std::string> PhraseIndexer::find_in_text(const std::string& text) const {
    std::vector<std::string> found_phrases;
    const size_t len = text.length();
    
    for (size_t i = 0; i < len; ++i) {
        const TrieNode* node = root.get();
        
        for (size_t j = i; j < len; ++j) {
            const unsigned char uc = static_cast<unsigned char>(
                std::tolower(static_cast<unsigned char>(text[j])));
            if (uc >= 128 || !node->children[uc]) break;
            node = node->children[uc].get();
            if (node->is_end_of_phrase) {
                found_phrases.push_back(node->phrase);
            }
        }
    }
    
    // Remove duplicates
    std::sort(found_phrases.begin(), found_phrases.end());
    found_phrases.erase(std::unique(found_phrases.begin(), found_phrases.end()),
                       found_phrases.end());
    
    return found_phrases;
}

std::vector<std::string> PhraseIndexer::get_all_phrases() const {
    std::vector<std::string> result;
    dfs_collect_phrases(root.get(), "", result);
    return result;
}

void PhraseIndexer::clear() {
    root = std::make_unique<TrieNode>();
    phrase_count_ = 0;
}

bool PhraseIndexer::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string phrase;
    while (std::getline(file, phrase)) {
        if (!phrase.empty()) {
            phrase.erase(0, phrase.find_first_not_of(" \t\r\n"));
            phrase.erase(phrase.find_last_not_of(" \t\r\n") + 1);
            if (!phrase.empty()) {
                insert_phrase(phrase);
            }
        }
    }
    
    return true;
}

} // namespace newsscope
