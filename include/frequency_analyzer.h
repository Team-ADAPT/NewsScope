#ifndef NEWSSCOPE_FREQUENCY_ANALYZER_H
#define NEWSSCOPE_FREQUENCY_ANALYZER_H

#include "types.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace newsscope {

class FrequencyAnalyzer {
public:
    FrequencyAnalyzer();
    
    void analyze(const std::vector<std::string>& tokens,
                 const std::string& normalized_text = "");
    void add_negative_term(const std::string& term, double weight = 0.5);
    int get_frequency(const std::string& term) const;
    double get_suspicion_score();
    std::vector<FrequencyEntry> get_top_negative_terms(size_t count = 10);
    bool load_negative_terms_from_file(const std::string& filename);
    void clear();
    double get_final_score();
    
private:
    std::unordered_map<std::string, int> frequency_map;
    std::unordered_map<std::string, double> negative_term_weights;
    std::unordered_map<std::string, double> suspicion_cache;
    
    void update_suspicion_cache();
    bool should_track_term(const std::string& term, double weight) const;
};

}

#endif
