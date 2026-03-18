#ifndef NEWSSCOPE_PHRASE_INDEXER_H
#define NEWSSCOPE_PHRASE_INDEXER_H

#include "types.h"
#include <string>
#include <vector>
#include <memory>
#include <array>

namespace newsscope {

class TrieNode {
public:
    // Direct array indexed by char value (ASCII 0-127); no hash overhead
    std::array<std::unique_ptr<TrieNode>, 128> children{};
    bool is_end_of_phrase = false;
    std::string phrase;
};

class PhraseIndexer {
public:
    PhraseIndexer();
    
    void insert_phrase(const std::string& phrase);
    bool phrase_exists(const std::string& phrase) const;
    std::vector<std::string> find_by_prefix(const std::string& prefix) const;
    std::vector<std::string> find_in_text(const std::string& text) const;
    std::vector<std::string> get_all_phrases() const;
    void clear();
    size_t phrase_count() const { return phrase_count_; }
    bool load_from_file(const std::string& filename);
    
private:
    std::unique_ptr<TrieNode> root;
    size_t phrase_count_;
    
    void dfs_collect_phrases(const TrieNode* node,
                            const std::string& prefix,
                            std::vector<std::string>& result) const;
};

}

#endif
