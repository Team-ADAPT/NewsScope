#ifndef NEWSSCOPE_STRING_MATCHER_H
#define NEWSSCOPE_STRING_MATCHER_H

#include <string>
#include <vector>

namespace newsscope {

class StringMatcher {
public:
    // KMP Algorithm - O(n+m) time, O(m) space
    static std::vector<int> build_lps_array(const std::string& pattern);
    static std::vector<size_t> kmp_search(const std::string& text, const std::string& pattern);
    static bool kmp_exists(const std::string& text, const std::string& pattern);
    
    // Rabin-Karp Algorithm - O(n+m) avg, O(nm) worst
    static constexpr unsigned long long PRIME_MODULUS = 1000000007ULL;
    static constexpr unsigned long long BASE = 256ULL;
    
    static std::vector<size_t> rabin_karp_search(const std::string& text, const std::string& pattern);
    static std::vector<std::pair<size_t, size_t>> rabin_karp_multi_search(
        const std::string& text, const std::vector<std::string>& patterns);
    static bool rabin_karp_exists(const std::string& text, const std::string& pattern);
    
private:
    static unsigned long long compute_hash(const std::string& s, size_t start, size_t end);
    static unsigned long long rolling_hash(unsigned long long prev_hash, char prev_char,
                                          char new_char, unsigned long long pow_base);
};

}

#endif
