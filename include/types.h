#ifndef NEWSSCOPE_TYPES_H
#define NEWSSCOPE_TYPES_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>

namespace newsscope {

struct Article {
    std::string id;
    std::string headline;
    std::string body;
    std::string source;
    std::chrono::system_clock::time_point timestamp;
    
    Article() = default;
    Article(const std::string& id, const std::string& headline, 
            const std::string& body, const std::string& source)
        : id(id), headline(headline), body(body), source(source),
          timestamp(std::chrono::system_clock::now()) {}
};

struct CredibilityResult {
    double overall_score = 0.0;
    double deterministic_score = 0.0;
    double ml_score = -1.0;
    std::unordered_map<std::string, double> module_scores;
    std::vector<std::string> explanations;
    std::chrono::milliseconds processing_time;
};

struct FrequencyEntry {
    std::string term;
    int frequency;
    double suspicion_level;
};

struct TimeWindowEntry {
    std::chrono::system_clock::time_point timestamp;
    std::string term;
    int frequency;
};

struct GreedySignal {
    std::string pattern_name;
    double severity;
    bool operator<(const GreedySignal& other) const {
        return severity > other.severity;
    }
};

struct FrequencyAnalysisResult {
    double suspicion_score = 0.0;
    std::vector<FrequencyEntry> top_negative_terms;
    std::unordered_map<std::string, int> frequency_map;
};

struct GreedyAnalysisResult {
    double manipulation_score = 0.0;
    std::vector<GreedySignal> detected_signals;
};

}

#endif
