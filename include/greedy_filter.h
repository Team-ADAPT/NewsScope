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
    
    std::vector<GreedySignal> detect_patterns(const std::string& headline);
    double calculate_manipulation_score(const std::vector<GreedySignal>& signals);
    void add_pattern_rule(const std::string& pattern_name,
                         std::function<bool(const std::string&)> detector,
                         double severity);
    double analyze_article(const std::string& headline, const std::string& body);
    std::vector<GreedySignal> get_detected_signals() const;
    void clear();
    
private:
    std::vector<GreedySignal> detected_signals;
    std::vector<std::tuple<std::string, std::function<bool(const std::string&)>, double>> custom_rules;
    
    bool detect_all_caps(const std::string& text);
    bool detect_excessive_exclamation(const std::string& text);
    bool detect_excessive_question(const std::string& text);
    bool detect_sensational_words(const std::string& text);
    bool detect_clickbait_structure(const std::string& text);
    bool detect_urgency_tactics(const std::string& text);
    bool detect_emotional_manipulation(const std::string& text);
    
    int count_char(const std::string& text, char c) const;
    bool contains_word(const std::string& text, const std::string& word) const;
    std::vector<std::string> get_sensational_words() const;
};

}

#endif
