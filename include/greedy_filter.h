#ifndef NEWSSCOPE_GREEDY_FILTER_H
#define NEWSSCOPE_GREEDY_FILTER_H

#include "types.h"
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <functional>

namespace newsscope {

class GreedyFilter {
public:
    enum class PatternType {
        ALL_CAPS,
        EXCESSIVE_EXCLAMATION,
        EXCESSIVE_QUESTION,
        SENSATIONAL_WORDS,
        CLICKBAIT_STRUCTURE,
        URGENCY_TACTICS,
        EMOTIONAL_MANIPULATION
    };
    
    GreedyFilter();
    
    std::vector<GreedySignal> detect_patterns(const std::string& headline) const;
    double calculate_manipulation_score(const std::vector<GreedySignal>& signals) const;
    void add_pattern_rule(const std::string& pattern_name,
                         std::function<bool(const std::string&)> detector,
                         double severity);
    GreedyAnalysisResult analyze_article(const std::string& headline, const std::string& body) const;
    
    std::vector<std::tuple<std::string, std::function<bool(const std::string&)>, double>> custom_rules;
    
    bool detect_all_caps(const std::string& text) const;
    bool detect_excessive_exclamation(const std::string& text) const;
    bool detect_excessive_question(const std::string& text) const;
    bool detect_sensational_words(const std::string& text) const;
    bool detect_clickbait_structure(const std::string& text) const;
    bool detect_urgency_tactics(const std::string& text) const;
    bool detect_emotional_manipulation(const std::string& text) const;
    
    int count_char(const std::string& text, char c) const;
    bool contains_word(const std::string& text, const std::string& word) const;
    std::vector<std::string> get_sensational_words() const;
};

}

#endif
