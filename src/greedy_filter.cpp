#include "greedy_filter.h"
#include <algorithm>
#include <cctype>
#include <queue>
#include <tuple>

namespace newsscope {

GreedyFilter::GreedyFilter() {}

bool GreedyFilter::detect_all_caps(const std::string& text) {
    int cap_count = 0;
    int letter_count = 0;
    
    for (char c : text) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            letter_count++;
            if (std::isupper(static_cast<unsigned char>(c))) {
                cap_count++;
            }
        }
    }
    
    return letter_count > 0 && (double)cap_count / letter_count > 0.5;
}

bool GreedyFilter::detect_excessive_exclamation(const std::string& text) {
    int exclamation_count = count_char(text, '!');
    return exclamation_count > 2;
}

bool GreedyFilter::detect_excessive_question(const std::string& text) {
    int question_count = count_char(text, '?');
    return question_count > 2;
}

bool GreedyFilter::detect_sensational_words(const std::string& text) {
    static const std::vector<std::string> sensational = {
        "unprecedented", "unbelievable", "shocking", "astonishing",
        "incredible", "amazing", "stunning", "astounding",
        "bombshell", "explosive", "sensational", "jaw-dropping",
        "mind-blowing"
    };
    for (const auto& word : sensational) {
        if (text.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool GreedyFilter::detect_clickbait_structure(const std::string& text) {
    static const std::vector<std::string> clickbait_patterns = {
        "you won't believe", "this one trick", "doctors hate", "click here",
        "see what", "find out", "what happens next", "you won't", "shocking truth"
    };
    for (const auto& pattern : clickbait_patterns) {
        if (text.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool GreedyFilter::detect_urgency_tactics(const std::string& text) {
    static const std::vector<std::string> urgency_words = {
        "immediate", "urgent", "limited time",
        "hurry", "don't wait", "act now", "last chance"
    };
    int urgency_count = 0;
    for (const auto& word : urgency_words) {
        if (text.find(word) != std::string::npos) {
            urgency_count++;
        }
    }
    return urgency_count >= 2;
}

bool GreedyFilter::detect_emotional_manipulation(const std::string& text) {
    static const std::vector<std::string> emotional_words = {
        "disgusting", "outrageous", "horrified", "enraged", "heartbroken",
        "devastated", "shocked", "appalled", "furious", "sickening"
    };
    int emotional_count = 0;
    for (const auto& word : emotional_words) {
        if (text.find(word) != std::string::npos) {
            emotional_count++;
        }
    }
    return emotional_count >= 2;
}

int GreedyFilter::count_char(const std::string& text, char c) const {
    int count = 0;
    for (char ch : text) {
        if (ch == c) count++;
    }
    return count;
}

bool GreedyFilter::contains_word(const std::string& text, const std::string& word) const {
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    std::string lower_word = word;
    std::transform(lower_word.begin(), lower_word.end(), lower_word.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    return lower_text.find(lower_word) != std::string::npos;
}

std::vector<std::string> GreedyFilter::get_sensational_words() const {
    return {
        "unprecedented", "unbelievable", "shocking", "astonishing",
        "incredible", "amazing", "stunning", "astounding",
        "bombshell", "explosive", "sensational", "jaw-dropping",
        "mind-blowing"
    };
}

std::vector<GreedySignal> GreedyFilter::detect_patterns(const std::string& headline) {
    detected_signals.clear();

    std::string lower = headline;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (detect_all_caps(headline)) {
        detected_signals.push_back({"ALL_CAPS", 0.8});
    }
    
    if (detect_excessive_exclamation(lower)) {
        detected_signals.push_back({"EXCESSIVE_EXCLAMATION", 0.7});
    }
    
    if (detect_excessive_question(lower)) {
        detected_signals.push_back({"EXCESSIVE_QUESTION", 0.6});
    }
    
    if (detect_sensational_words(lower)) {
        detected_signals.push_back({"SENSATIONAL_WORDS", 0.75});
    }
    
    if (detect_clickbait_structure(lower)) {
        detected_signals.push_back({"CLICKBAIT_STRUCTURE", 0.85});
    }
    
    if (detect_urgency_tactics(lower)) {
        detected_signals.push_back({"URGENCY_TACTICS", 0.7});
    }
    
    if (detect_emotional_manipulation(lower)) {
        detected_signals.push_back({"EMOTIONAL_MANIPULATION", 0.75});
    }

    for (const auto& rule : custom_rules) {
        const auto& name = std::get<0>(rule);
        const auto& detector = std::get<1>(rule);
        double severity = std::get<2>(rule);
        if (detector(headline)) {
            detected_signals.push_back({name, std::max(0.0, std::min(1.0, severity))});
        }
    }
    
    return detected_signals;
}

double GreedyFilter::calculate_manipulation_score(const std::vector<GreedySignal>& signals) {
    if (signals.empty()) {
        return 0.0;  // No manipulation detected
    }
    
    // Greedy approach: use the most severe signals
    std::priority_queue<GreedySignal> pq;
    
    for (const auto& signal : signals) {
        pq.push(signal);
    }
    
    double total_score = 0.0;
    double weight = 1.0;
    int count = 0;
    
    while (!pq.empty() && count < 3) {  // Top 3 signals
        auto signal = pq.top();
        pq.pop();
        
        total_score += signal.severity * weight * 100.0;
        weight *= 0.5;  // Diminishing returns for subsequent signals
        count++;
    }
    
    return std::max(0.0, std::min(100.0, total_score));
}

double GreedyFilter::analyze_article(const std::string& headline, const std::string& body) {
    // Lowercase once per string, reuse across all detectors
    std::string lower_headline = headline;
    std::transform(lower_headline.begin(), lower_headline.end(), lower_headline.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::string lower_body = body;
    std::transform(lower_body.begin(), lower_body.end(), lower_body.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // detect_all_caps needs original casing; other detectors use lowercased text
    detected_signals.clear();

    if (detect_all_caps(headline)) {
        detected_signals.push_back({"ALL_CAPS", std::min(1.0, 0.8 * 1.5)});
    }
    auto push_if = [&](bool triggered, const std::string& name, double sev, double headline_mult) {
        if (triggered) detected_signals.push_back({name, std::min(1.0, sev * headline_mult)});
    };
    push_if(detect_excessive_exclamation(lower_headline), "EXCESSIVE_EXCLAMATION", 0.7, 1.5);
    push_if(detect_excessive_question(lower_headline),    "EXCESSIVE_QUESTION",    0.6, 1.5);
    push_if(detect_sensational_words(lower_headline),     "SENSATIONAL_WORDS",     0.75, 1.5);
    push_if(detect_clickbait_structure(lower_headline),   "CLICKBAIT_STRUCTURE",   0.85, 1.5);
    push_if(detect_urgency_tactics(lower_headline),       "URGENCY_TACTICS",       0.7, 1.5);
    push_if(detect_emotional_manipulation(lower_headline),"EMOTIONAL_MANIPULATION",0.75, 1.5);

    if (detect_all_caps(body)) {
        detected_signals.push_back({"ALL_CAPS", 0.8});
    }
    push_if(detect_excessive_exclamation(lower_body), "EXCESSIVE_EXCLAMATION", 0.7, 1.0);
    push_if(detect_excessive_question(lower_body),    "EXCESSIVE_QUESTION",    0.6, 1.0);
    push_if(detect_sensational_words(lower_body),     "SENSATIONAL_WORDS",     0.75, 1.0);
    push_if(detect_clickbait_structure(lower_body),   "CLICKBAIT_STRUCTURE",   0.85, 1.0);
    push_if(detect_urgency_tactics(lower_body),       "URGENCY_TACTICS",       0.7, 1.0);
    push_if(detect_emotional_manipulation(lower_body),"EMOTIONAL_MANIPULATION",0.75, 1.0);

    for (const auto& rule : custom_rules) {
        const auto& name     = std::get<0>(rule);
        const auto& detector = std::get<1>(rule);
        double severity      = std::get<2>(rule);
        if (detector(lower_headline)) detected_signals.push_back({name, std::min(1.0, severity * 1.5)});
        else if (detector(lower_body)) detected_signals.push_back({name, std::max(0.0, std::min(1.0, severity))});
    }

    return calculate_manipulation_score(detected_signals);
}

void GreedyFilter::add_pattern_rule(const std::string& pattern_name,
                                   std::function<bool(const std::string&)> detector,
                                   double severity) {
    custom_rules.emplace_back(pattern_name, std::move(detector), severity);
}

std::vector<GreedySignal> GreedyFilter::get_detected_signals() const {
    return detected_signals;
}

void GreedyFilter::clear() {
    detected_signals.clear();
}

} // namespace newsscope
