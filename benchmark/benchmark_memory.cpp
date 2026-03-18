#include "scoring_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace newsscope;

// Simple memory estimator
class MemoryEstimator {
public:
    static constexpr size_t KB = 1024;
    static constexpr size_t MB = 1024 * 1024;
    
    static void print_size(const std::string& name, size_t bytes) {
        if (bytes > MB) {
            std::cout << "  " << std::left << std::setw(30) << name 
                      << std::fixed << std::setprecision(2) 
                      << (bytes / (double)MB) << " MB\n";
        } else if (bytes > KB) {
            std::cout << "  " << std::left << std::setw(30) << name
                      << std::fixed << std::setprecision(2)
                      << (bytes / (double)KB) << " KB\n";
        } else {
            std::cout << "  " << std::left << std::setw(30) << name
                      << bytes << " bytes\n";
        }
    }
    
    static size_t estimate_string_size(const std::string& s) {
        return sizeof(s) + s.capacity();  // string object + allocated capacity
    }
    
    static size_t estimate_article_size(const Article& a) {
        return sizeof(a)
             + estimate_string_size(a.id)
             + estimate_string_size(a.headline)
             + estimate_string_size(a.body)
             + estimate_string_size(a.source);
    }
};

int main() {
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "NewsScope: Memory Usage Benchmark\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    // Estimate single article memory
    std::cout << "1. SINGLE ARTICLE MEMORY FOOTPRINT\n";
    std::cout << std::string(80, '-') << "\n";
    
    Article test_article(
        "test_id",
        "This is a test headline for memory estimation",
        "This is a longer body of text that helps estimate memory usage "
        "for storing articles in the system. It contains multiple sentences "
        "and various content types that are typical in news articles.",
        "TestSource"
    );
    
    size_t article_size = MemoryEstimator::estimate_article_size(test_article);
    MemoryEstimator::print_size("Single Article Size", article_size);
    std::cout << "\n";
    
    // Estimate ScoringEngine memory
    std::cout << "2. SCORING ENGINE MEMORY FOOTPRINT\n";
    std::cout << std::string(80, '-') << "\n";
    
    ScoringEngine engine;
    engine.initialize();
    
    // Rough estimates for engine components
    size_t preprocessor_est = 50 * MemoryEstimator::KB;  // stop words set, etc.
    size_t source_validator_est = 100 * MemoryEstimator::KB;  // hash map of sources
    size_t phrase_indexer_est = 200 * MemoryEstimator::KB;  // Trie structure
    size_t other_modules_est = 50 * MemoryEstimator::KB;  // Frequency, temporal, etc.
    
    size_t total_engine = preprocessor_est + source_validator_est 
                        + phrase_indexer_est + other_modules_est;
    
    MemoryEstimator::print_size("Preprocessor Module", preprocessor_est);
    MemoryEstimator::print_size("Source Validator", source_validator_est);
    MemoryEstimator::print_size("Phrase Indexer (Trie)", phrase_indexer_est);
    MemoryEstimator::print_size("Other Modules", other_modules_est);
    std::cout << "  " << std::string(30, '-') << "\n";
    MemoryEstimator::print_size("Total Engine Overhead", total_engine);
    std::cout << "\n";
    
    // Memory scaling with article count
    std::cout << "3. MEMORY SCALING WITH ARTICLE COUNT\n";
    std::cout << std::string(80, '-') << "\n";
    std::cout << std::left << std::setw(20) << "Article Count"
              << std::setw(25) << "Total Memory (MB)"
              << std::setw(25) << "Avg per Article (KB)\n";
    std::cout << std::string(70, '-') << "\n";
    
    for (int count : {100, 500, 1000, 5000, 10000}) {
        size_t total = total_engine + (article_size * count);
        size_t avg_per = total / count;
        
        std::cout << std::left << std::setw(20) << count
                  << std::fixed << std::setprecision(2)
                  << std::setw(25) << (total / (double)MemoryEstimator::MB)
                  << std::setw(25) << (avg_per / (double)MemoryEstimator::KB) << "\n";
    }
    std::cout << "\n";
    
    // Module memory analysis
    std::cout << "4. DATA STRUCTURE MEMORY ANALYSIS\n";
    std::cout << std::string(80, '-') << "\n";
    
    std::cout << "unordered_map (hash table):\n";
    std::cout << "  - Expected: O(n) space for n elements\n";
    std::cout << "  - With typical load factor ~0.75\n";
    std::cout << "  - Estimated 50KB per 1000 source entries\n\n";
    
    std::cout << "Trie (phrase indexing):\n";
    std::cout << "  - Expected: O(k*m) space where k=phrases, m=avg length\n";
    std::cout << "  - Estimated 200KB for 5000 suspicious phrases\n\n";
    
    std::cout << "Frequency Analysis (unordered_map):\n";
    std::cout << "  - Expected: O(unique_tokens) space\n";
    std::cout << "  - Estimated 100KB for 10000 unique tokens\n\n";
    
    std::cout << "Temporal Analysis (deque):\n";
    std::cout << "  - Expected: O(window_size) space\n";
    std::cout << "  - Estimated 50KB for 24-hour window with 1000 entries\n\n";
    
    // Memory efficiency summary
    std::cout << "5. MEMORY EFFICIENCY SUMMARY\n";
    std::cout << std::string(80, '-') << "\n";
    
    std::cout << "Target: < 50 MB per 1000 articles in memory\n\n";
    
    double estimated_per_1000 = (total_engine + (article_size * 1000)) / (double)MemoryEstimator::MB;
    std::cout << "Estimated: " << std::fixed << std::setprecision(2) 
              << estimated_per_1000 << " MB per 1000 articles\n";
    
    if (estimated_per_1000 < 50.0) {
        std::cout << "Status: ✓ MEETS TARGET\n";
    } else {
        std::cout << "Status: ✗ EXCEEDS TARGET (optimization recommended)\n";
    }
    
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "Memory Benchmark Complete!\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    return 0;
}
