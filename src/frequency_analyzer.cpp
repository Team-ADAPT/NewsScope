#include "frequency_analyzer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_set>

namespace {

std::string normalize_term(std::string term) {
    const auto first = term.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = term.find_last_not_of(" \t\r\n");
    term = term.substr(first, last - first + 1);
    std::transform(term.begin(), term.end(), term.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return term;
}

bool is_word_boundary(char c) {
    return !std::isalnum(static_cast<unsigned char>(c));
}

bool is_generic_context_term(const std::string& term) {
    static const std::unordered_set<std::string> generic_terms = {
        "crisis", "crises", "scandal", "exposed", "attack", "attacks",
        "war", "conflict", "breaking", "leaked", "revealed", "urgent",
        "government", "officials", "report"
    };
    return generic_terms.find(term) != generic_terms.end();
}

int count_term_occurrences(const std::string& text, const std::string& term) {
    if (text.empty() || term.empty()) {
        return 0;
    }

    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(term, pos)) != std::string::npos) {
        const bool left_ok = (pos == 0) || is_word_boundary(text[pos - 1]);
        const size_t end = pos + term.size();
        const bool right_ok = (end >= text.size()) || is_word_boundary(text[end]);
        if (left_ok && right_ok) {
            ++count;
        }
        pos = pos + term.size();
    }
    return count;
}

} // namespace

namespace newsscope {

FrequencyAnalyzer::FrequencyAnalyzer() {
    add_negative_term("fake", 0.8);
    add_negative_term("hoax", 0.9);
    add_negative_term("conspiracy", 0.7);
    add_negative_term("scandal", 0.6);
    add_negative_term("exposed", 0.7);
    add_negative_term("shocking", 0.6);
    add_negative_term("unbelievable", 0.6);
    add_negative_term("outrageous", 0.6);
}

FrequencyAnalysisResult FrequencyAnalyzer::analyze(const std::vector<std::string>& tokens,
                                                 const std::string& normalized_text) const {
    FrequencyAnalysisResult result;
    std::unordered_map<std::string, int>& frequency_map = result.frequency_map;
    
    for (const auto& token : tokens) {
        frequency_map[token]++;
    }

    if (!normalized_text.empty()) {
        for (const auto& entry : negative_term_weights) {
            const std::string& term = entry.first;
            if (term.find(' ') == std::string::npos) {
                continue;
            }
            const int occurrences = count_term_occurrences(normalized_text, term);
            if (occurrences > 0) {
                frequency_map[term] += occurrences;
            }
        }
    }
    
    std::unordered_map<std::string, double> suspicion_cache;
    double total_suspicion = 0.0;

    for (const auto& freq_entry : frequency_map) {
        const auto weight_it = negative_term_weights.find(freq_entry.first);
        if (weight_it == negative_term_weights.end()) {
            continue;
        }
        const double weight = weight_it->second;
        const int frequency = freq_entry.second;
        const double suspicion = weight * (1.0 - std::exp(-0.3 * frequency));
        const double suspicion_scaled = suspicion * 100.0;
        suspicion_cache[freq_entry.first] = suspicion_scaled;
        total_suspicion += suspicion_scaled;
    }
    
    result.suspicion_score = std::min(100.0, total_suspicion);

    for (const auto& freq_entry : frequency_map) {
        const auto weight_it = negative_term_weights.find(freq_entry.first);
        if (weight_it == negative_term_weights.end() || freq_entry.second <= 0) {
            continue;
        }

        double suspicion_level = 0.0;
        const auto cache_it = suspicion_cache.find(freq_entry.first);
        if (cache_it != suspicion_cache.end()) {
            suspicion_level = cache_it->second;
        }

        result.top_negative_terms.push_back({freq_entry.first, freq_entry.second, suspicion_level});
    }
    
    std::sort(result.top_negative_terms.begin(), result.top_negative_terms.end(),
              [](const FrequencyEntry& a, const FrequencyEntry& b) {
                  return a.suspicion_level > b.suspicion_level;
              });
    
    if (result.top_negative_terms.size() > 3) {
        result.top_negative_terms.resize(3);
    }

    return result;
}

void FrequencyAnalyzer::add_negative_term(const std::string& term, double weight) {
    const std::string normalized = normalize_term(term);
    const double clamped_weight = std::max(0.0, std::min(1.0, weight));
    if (normalized.empty() || !should_track_term(normalized, clamped_weight)) {
        return;
    }
    negative_term_weights[normalized] = clamped_weight;
}

bool FrequencyAnalyzer::load_negative_terms_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        std::string term;
        double weight;
        
        if (std::getline(iss, term, ',') && iss >> weight) {
            add_negative_term(term, weight);
        }
    }
    
    file.close();
    return true;
}

bool FrequencyAnalyzer::should_track_term(const std::string& term, double weight) const {
    if (term.find(' ') != std::string::npos) {
        return true;
    }
    if (is_generic_context_term(term)) {
        return false;
    }
    return weight >= 0.70;
}

}
