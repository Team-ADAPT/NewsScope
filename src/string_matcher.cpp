#include "string_matcher.h"
#include <algorithm>

namespace newsscope {

// ============================================================================
// KMP IMPLEMENTATION
// ============================================================================

std::vector<int> StringMatcher::build_lps_array(const std::string& pattern) {
    int m = pattern.length();
    std::vector<int> lps(m, 0);
    
    int len = 0;  // Length of previous longest prefix suffix
    int i = 1;
    
    while (i < m) {
        if (pattern[i] == pattern[len]) {
            len++;
            lps[i] = len;
            i++;
        } else {
            if (len != 0) {
                len = lps[len - 1];
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }
    
    return lps;
}

std::vector<size_t> StringMatcher::kmp_search(const std::string& text,
                                              const std::string& pattern) {
    std::vector<size_t> matches;
    
    if (pattern.empty() || text.empty() || pattern.length() > text.length()) {
        return matches;
    }
    
    int n = text.length();
    int m = pattern.length();
    std::vector<int> lps = build_lps_array(pattern);
    
    int i = 0;  // Index for text
    int j = 0;  // Index for pattern
    
    while (i < n) {
        if (text[i] == pattern[j]) {
            i++;
            j++;
        }
        
        if (j == m) {
            matches.push_back(i - j);
            j = lps[j - 1];
        } else if (i < n && text[i] != pattern[j]) {
            if (j != 0) {
                j = lps[j - 1];
            } else {
                i++;
            }
        }
    }
    
    return matches;
}

bool StringMatcher::kmp_exists(const std::string& text, const std::string& pattern) {
    if (pattern.empty() || text.empty()) {
        return pattern.empty();
    }
    
    auto matches = kmp_search(text, pattern);
    return !matches.empty();
}

// ============================================================================
// RABIN-KARP IMPLEMENTATION
// ============================================================================

unsigned long long StringMatcher::compute_hash(const std::string& s, 
                                               size_t start, size_t end) {
    unsigned long long hash = 0;
    for (size_t i = start; i < end; ++i) {
        hash = (hash * BASE + static_cast<unsigned long long>(static_cast<unsigned char>(s[i]))) % PRIME_MODULUS;
    }
    return hash;
}

unsigned long long StringMatcher::rolling_hash(unsigned long long prev_hash,
                                              char prev_char,
                                              char new_char,
                                              unsigned long long pow_base) {
    // Remove the leftmost character's contribution
    unsigned long long outgoing = (static_cast<unsigned long long>(static_cast<unsigned char>(prev_char)) * pow_base) % PRIME_MODULUS;
    unsigned long long hash = (prev_hash + PRIME_MODULUS - outgoing) % PRIME_MODULUS;
    
    // Shift left and add new character
    hash = (hash * BASE + static_cast<unsigned long long>(static_cast<unsigned char>(new_char))) % PRIME_MODULUS;
    return hash;
}

std::vector<size_t> StringMatcher::rabin_karp_search(const std::string& text,
                                                     const std::string& pattern) {
    std::vector<size_t> matches;
    
    if (pattern.empty() || text.empty() || pattern.length() > text.length()) {
        return matches;
    }
    
    int n = text.length();
    int m = pattern.length();
    
    // Precompute BASE^(m-1) % PRIME_MODULUS
    unsigned long long pow_base = 1;
    for (int i = 0; i < m - 1; ++i) {
        pow_base = (pow_base * BASE) % PRIME_MODULUS;
    }
    
    // Compute pattern hash
    unsigned long long pattern_hash = compute_hash(pattern, 0, m);
    unsigned long long text_hash = compute_hash(text, 0, m);
    
    // Check first window
    if (pattern_hash == text_hash &&
        text.compare(0, m, pattern) == 0) {
        matches.push_back(0);
    }
    
    // Roll the hash through the text
    for (int i = 0; i < n - m; ++i) {
        text_hash = rolling_hash(text_hash, text[i], text[i + m], pow_base);
        
        if (text_hash == pattern_hash &&
            text.compare(i + 1, m, pattern) == 0) {
            matches.push_back(i + 1);
        }
    }
    
    return matches;
}

std::vector<std::pair<size_t, size_t>>
StringMatcher::rabin_karp_multi_search(const std::string& text,
                                      const std::vector<std::string>& patterns) {
    std::vector<std::pair<size_t, size_t>> results;
    
    for (size_t p = 0; p < patterns.size(); ++p) {
        auto matches = rabin_karp_search(text, patterns[p]);
        for (size_t pos : matches) {
            results.push_back({pos, p});  // (position, pattern_index)
        }
    }
    
    return results;
}

bool StringMatcher::rabin_karp_exists(const std::string& text,
                                     const std::string& pattern) {
    if (pattern.empty() || text.empty()) {
        return pattern.empty();
    }
    
    auto matches = rabin_karp_search(text, pattern);
    return !matches.empty();
}

} // namespace newsscope
