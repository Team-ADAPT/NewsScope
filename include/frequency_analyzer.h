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
    
    FrequencyAnalysisResult analyze(const std::vector<std::string>& tokens,
                                    const std::string& normalized_text = "") const;
    void add_negative_term(const std::string& term, double weight = 0.5);
    bool load_negative_terms_from_file(const std::string& filename);
    
private:
    std::unordered_map<std::string, double> negative_term_weights;
    bool should_track_term(const std::string& term, double weight) const;
};

}

#endif
