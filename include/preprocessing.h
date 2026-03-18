#ifndef NEWSSCOPE_PREPROCESSING_H
#define NEWSSCOPE_PREPROCESSING_H

#include "types.h"
#include <vector>
#include <string>
#include <unordered_set>

namespace newsscope {

class Preprocessor {
public:
    Preprocessor();
    
    std::vector<std::string> tokenize(const std::string& text);
    void remove_stop_words(std::vector<std::string>& tokens);
    std::string normalize_token(const std::string& token);
    std::vector<std::string> process(const Article& article);
    void add_stop_word(const std::string& word);
    void clear_stop_words();
    
private:
    std::unordered_set<std::string> stop_words;
    
    void initialize_stop_words();
    bool is_stop_word(const std::string& word) const;
};

}

#endif
