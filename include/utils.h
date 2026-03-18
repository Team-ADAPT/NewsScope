#ifndef NEWSSCOPE_UTILS_H
#define NEWSSCOPE_UTILS_H

#include <string>
#include <algorithm>
#include <cctype>

namespace newsscope {
namespace utils {

inline double clamp_score(double score, double min_val = 0.0, double max_val = 100.0) {
    return std::max(min_val, std::min(score, max_val));
}

inline std::string to_lower_copy(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

inline bool has_negated_prefix(const std::string& text, size_t pos, size_t window = 32) {
    if (pos == 0) return false;
    
    size_t start = (pos > window) ? (pos - window) : 0;
    std::string prefix = text.substr(start, pos - start);
    std::string prefix_lower = to_lower_copy(prefix);
    
    const std::vector<std::string> negation_markers = {
        "no ", "not ", "never ", "without ", "lacking ", "absence of ", "fails to ",
        "doesn't ", "don't ", "won't ", "can't ", "isn't ", "aren't "
    };
    
    for (const auto& marker : negation_markers) {
        if (prefix_lower.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline size_t count_phrase_hits(const std::string& text, const std::vector<std::string>& phrases, size_t negation_window = 32) {
    size_t count = 0;
    std::string text_lower = to_lower_copy(text);
    
    for (const auto& phrase : phrases) {
        size_t pos = text_lower.find(phrase);
        while (pos != std::string::npos) {
            if (!has_negated_prefix(text_lower, pos, negation_window)) {
                ++count;
            }
            pos = text_lower.find(phrase, pos + 1);
        }
    }
    return count;
}

inline size_t count_positive_phrase_hits(const std::string& text, const std::vector<std::string>& phrases, size_t negation_window = 32) {
    size_t count = 0;
    std::string text_lower = to_lower_copy(text);
    
    for (const auto& phrase : phrases) {
        size_t pos = text_lower.find(phrase);
        while (pos != std::string::npos) {
            if (!has_negated_prefix(text_lower, pos, negation_window)) {
                ++count;
            }
            pos = text_lower.find(phrase, pos + 1);
        }
    }
    return count;
}

} // namespace utils
} // namespace newsscope

#endif
