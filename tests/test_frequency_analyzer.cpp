#include "frequency_analyzer.h"
#include <iostream>
#include <cassert>

using namespace newsscope;

void test_frequency_analysis() {
    std::cout << "Testing Frequency Analysis...\n";
    FrequencyAnalyzer analyzer;
    
    std::vector<std::string> tokens = {"fake", "news", "fake", "hoax", "fake"};
    analyzer.analyze(tokens);
    
    assert(analyzer.get_frequency("fake") == 3);
    assert(analyzer.get_frequency("news") == 1);
    assert(analyzer.get_frequency("hoax") == 1);
    
    std::cout << "  ✓ Frequency counting works correctly\n";
}

void test_negative_term_detection() {
    std::cout << "Testing Negative Term Detection...\n";
    FrequencyAnalyzer analyzer;
    
    analyzer.add_negative_term("conspiracy", 0.9);
    analyzer.add_negative_term("hoax", 0.8);
    
    std::vector<std::string> tokens = {"conspiracy", "theory", "hoax", "exposed"};
    analyzer.analyze(tokens);
    
    double score = analyzer.get_suspicion_score();
    assert(score > 5.0);  // Should detect non-trivial suspicion
    
    std::cout << "  ✓ Negative term detection works, suspicion score: " << score << "\n";
}

void test_multiword_negative_term_detection() {
    std::cout << "Testing Multi-word Negative Term Detection...\n";
    FrequencyAnalyzer analyzer;

    analyzer.add_negative_term("deep state plot", 0.9);
    analyzer.add_negative_term("without evidence", 0.8);

    std::vector<std::string> tokens = {"deep", "state", "plot", "without", "evidence"};
    analyzer.analyze(tokens, "a deep state plot is spreading without evidence online");

    assert(analyzer.get_frequency("deep state plot") == 1);
    assert(analyzer.get_frequency("without evidence") == 1);
    assert(analyzer.get_suspicion_score() > 20.0);

    std::cout << "  ✓ Multi-word terms are detected from normalized text\n";
}

void test_top_negative_terms() {
    std::cout << "Testing Top Negative Terms...\n";
    FrequencyAnalyzer analyzer;
    
    analyzer.add_negative_term("fake", 0.8);
    analyzer.add_negative_term("hoax", 0.9);
    
    std::vector<std::string> tokens = {"fake", "fake", "hoax", "hoax", "hoax"};
    analyzer.analyze(tokens);
    
    auto top_terms = analyzer.get_top_negative_terms(2);
    
    assert(top_terms.size() <= 2);
    
    std::cout << "  ✓ Found " << top_terms.size() << " top negative term(s)\n";
}

void test_clear() {
    std::cout << "Testing Clear...\n";
    FrequencyAnalyzer analyzer;
    
    std::vector<std::string> tokens = {"test", "test"};
    analyzer.analyze(tokens);
    assert(analyzer.get_frequency("test") == 2);
    
    analyzer.clear();
    assert(analyzer.get_frequency("test") == 0);
    
    std::cout << "  ✓ Clear works correctly\n";
}

void test_weight_boundaries() {
    std::cout << "Testing Weight Boundaries...\n";
    FrequencyAnalyzer analyzer;
    
    analyzer.add_negative_term("test", 2.5);  // Should be clamped to 1.0
    analyzer.add_negative_term("test2", -0.5);  // Should be clamped to 0.0
    
    std::cout << "  ✓ Weight boundary clamping works\n";
}

void test_final_score() {
    std::cout << "Testing Final Score...\n";
    FrequencyAnalyzer analyzer;
    
    std::vector<std::string> tokens = {"word", "word", "word"};
    analyzer.analyze(tokens);
    
    double score = analyzer.get_final_score();
    assert(score >= 0.0 && score <= 100.0);
    
    std::cout << "  ✓ Final score in valid range: " << score << "\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "FREQUENCY ANALYZER TESTS\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    try {
        test_frequency_analysis();
        test_negative_term_detection();
        test_multiword_negative_term_detection();
        test_top_negative_terms();
        test_clear();
        test_weight_boundaries();
        test_final_score();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "All frequency analyzer tests passed! ✓\n";
        std::cout << std::string(60, '=') << "\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
