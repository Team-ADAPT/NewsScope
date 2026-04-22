#include "source_validator.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace {

std::vector<std::string> split_tokens(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    for (std::string token; iss >> token;) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string join_tokens(const std::vector<std::string>& tokens) {
    std::ostringstream out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << tokens[i];
    }
    return out.str();
}

bool is_common_prefix_token(const std::string& token) {
    static const std::set<std::string> prefixes = {
        "www", "m", "mobile", "amp", "edition"
    };
    return prefixes.find(token) != prefixes.end();
}

bool is_common_suffix_token(const std::string& token) {
    static const std::set<std::string> suffixes = {
        "com", "org", "net", "co", "io", "uk", "us", "in", "ca", "au",
        "news", "media"
    };
    return suffixes.find(token) != suffixes.end();
}

bool token_subset_match(const std::vector<std::string>& input_tokens,
                        const std::vector<std::string>& candidate_tokens) {
    if (input_tokens.empty() || candidate_tokens.empty() ||
        input_tokens.size() > candidate_tokens.size()) {
        return false;
    }

    std::set<std::string> candidate_set(candidate_tokens.begin(), candidate_tokens.end());
    for (const auto& token : input_tokens) {
        if (candidate_set.find(token) == candidate_set.end()) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace newsscope {

std::string SourceValidator::normalize_source_name(const std::string& source_name) const {
    std::string normalized;
    normalized.reserve(source_name.size());
    bool previous_space = false;

    for (char c : source_name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            normalized.push_back(static_cast<char>(std::tolower(uc)));
            previous_space = false;
            continue;
        }
        if (std::isspace(uc) || c == '.' || c == '/' || c == '-' || c == '_' || c == '|') {
            if (!previous_space && !normalized.empty()) {
                normalized.push_back(' ');
                previous_space = true;
            }
            continue;
        }
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    std::vector<std::string> tokens = split_tokens(normalized);
    while (!tokens.empty() && is_common_prefix_token(tokens.front())) {
        tokens.erase(tokens.begin());
    }
    while (tokens.size() > 1) {
        const std::string& tail = tokens.back();
        if (is_common_suffix_token(tail)) {
            tokens.pop_back();
            continue;
        }
        break;
    }
    if (tokens.size() > 1 && tokens.front() == "the") {
        tokens.erase(tokens.begin());
    }
    return join_tokens(tokens);
}

SourceValidator::SourceValidator() {
    add_trusted_source("BBC", 90.0);
    add_trusted_source("BBC News", 94.0);
    add_trusted_source("bbc.com", 94.0);
    add_trusted_source("Reuters", 96.0);
    add_trusted_source("reuters.com", 96.0);
    add_trusted_source("Associated Press", 96.0);
    add_trusted_source("AP", 96.0);
    add_trusted_source("AP News", 96.0);
    add_trusted_source("apnews.com", 96.0);
    add_trusted_source("The Guardian", 91.0);
    add_trusted_source("NPR", 91.0);
    add_trusted_source("npr.org", 91.0);
    add_trusted_source("Wall Street Journal", 90.0);
    add_trusted_source("WSJ", 90.0);
    add_trusted_source("wsj.com", 90.0);
    add_trusted_source("New York Times", 92.0);
    add_trusted_source("NYT", 92.0);
    add_trusted_source("nytimes.com", 92.0);
    add_trusted_source("cnn.com", 80.0);
    add_trusted_source("bloomberg.com", 92.0);
    
    add_untrusted_source("Fake News Daily", 15.0);
    add_untrusted_source("Hoax Report", 10.0);
}

double SourceValidator::validate_source(const std::string& source_name) {
    const std::string normalized = normalize_source_name(source_name);
    auto it = sources.find(normalized);
    if (it != sources.end()) {
        return it->second;
    }

    const std::vector<std::string> input_tokens = split_tokens(normalized);
    double best_score = -1.0;
    size_t best_overlap = 0;
    for (const auto& entry : sources) {
        const std::vector<std::string> candidate_tokens = split_tokens(entry.first);
        if (!token_subset_match(input_tokens, candidate_tokens)) {
            continue;
        }
        if (input_tokens.size() > best_overlap ||
            (input_tokens.size() == best_overlap && entry.second > best_score)) {
            best_overlap = input_tokens.size();
            best_score = entry.second;
        }
    }
    if (best_score >= 0.0) {
        return best_score;
    }
    return unknown_source_credibility;
}

void SourceValidator::add_trusted_source(const std::string& source_name, double credibility) {
    sources[normalize_source_name(source_name)] = credibility;
}

void SourceValidator::add_untrusted_source(const std::string& source_name, double credibility) {
    sources[normalize_source_name(source_name)] = credibility;
}

void SourceValidator::add_neutral_source(const std::string& source_name, double credibility) {
    sources[normalize_source_name(source_name)] = credibility;
}

bool SourceValidator::source_exists(const std::string& source_name) const {
    return sources.find(normalize_source_name(source_name)) != sources.end();
}

SourceValidator::SourceStatus SourceValidator::get_source_status(const std::string& source_name) const {
    auto it = sources.find(normalize_source_name(source_name));
    if (it == sources.end()) {
        return SourceStatus::NEUTRAL;
    }
    
    double credibility = it->second;
    if (credibility >= 70.0) {
        return SourceStatus::TRUSTED;
    } else if (credibility <= 35.0) {
        return SourceStatus::UNTRUSTED;
    }
    return SourceStatus::NEUTRAL;
}

bool SourceValidator::load_from_csv(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        std::string source_name, status;
        double credibility;
        
        if (std::getline(iss, source_name, ',') &&
            std::getline(iss, status, ',') &&
            iss >> credibility) {
            sources[normalize_source_name(source_name)] = credibility;
        }
    }
    
    file.close();
    return true;
}

std::unordered_map<std::string, double> SourceValidator::get_all_sources() const {
    return sources;
}

void SourceValidator::clear() {
    sources.clear();
}

} // namespace newsscope
