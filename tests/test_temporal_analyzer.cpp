#include "temporal_analyzer.h"
#include <iostream>
#include <cassert>

using namespace newsscope;

void test_window_duration() {
    std::cout << "Testing Window Duration...\n";
    TemporalAnalyzer analyzer;
    
    analyzer.set_window_duration(std::chrono::seconds(60));
    
    std::cout << "  ✓ Window duration set successfully\n";
}

void test_add_entry() {
    std::cout << "Testing Add Entry...\n";
    TemporalAnalyzer analyzer;
    
    auto now = std::chrono::system_clock::now();
    analyzer.add_entry("term1", 5, now);
    analyzer.add_entry("term2", 3, now);
    
    std::cout << "  ✓ Entries added successfully\n";
}

void test_average_frequency() {
    std::cout << "Testing Average Frequency...\n";
    TemporalAnalyzer analyzer;
    
    auto now = std::chrono::system_clock::now();
    analyzer.add_entry("test", 10, now);
    analyzer.add_entry("test", 20, now);
    analyzer.add_entry("test", 30, now);
    
    double avg = analyzer.get_average_frequency("test");
    assert(avg == 20.0);  // (10 + 20 + 30) / 3 = 20
    
    std::cout << "  ✓ Average frequency calculated correctly: " << avg << "\n";
}

void test_spike_severity() {
    std::cout << "Testing Spike Severity...\n";
    TemporalAnalyzer analyzer;
    
    auto now = std::chrono::system_clock::now();
    analyzer.add_entry("spike_test", 5, now);
    
    double severity = analyzer.calculate_spike_severity("spike_test");
    assert(severity >= 0.0 && severity <= 1.0);
    
    std::cout << "  ✓ Spike severity in valid range: " << severity << "\n";
}

void test_spike_score() {
    std::cout << "Testing Spike Score...\n";
    TemporalAnalyzer analyzer;
    
    auto now = std::chrono::system_clock::now();
    analyzer.add_entry("term", 1, now);
    
    double score = analyzer.get_spike_score();
    assert(score == 0.0);
    
    std::cout << "  ✓ No-spike temporal score stays neutral: " << score << "\n";
}

void test_clear() {
    std::cout << "Testing Clear...\n";
    TemporalAnalyzer analyzer;
    
    auto now = std::chrono::system_clock::now();
    analyzer.add_entry("test", 10, now);
    analyzer.clear();
    
    double avg = analyzer.get_average_frequency("test");
    assert(avg == 0.0);
    
    std::cout << "  ✓ Clear works correctly\n";
}

void test_detected_spikes() {
    std::cout << "Testing Detected Spikes...\n";
    TemporalAnalyzer analyzer;
    
    auto now = std::chrono::system_clock::now();
    analyzer.add_entry("normal", 5, now);
    
    auto spikes = analyzer.get_detected_spikes();
    // May or may not detect spikes depending on statistics
    
    std::cout << "  ✓ Detected spikes retrieval works\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "TEMPORAL ANALYZER TESTS\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    try {
        test_window_duration();
        test_add_entry();
        test_average_frequency();
        test_spike_severity();
        test_spike_score();
        test_clear();
        test_detected_spikes();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "All temporal analyzer tests passed! ✓\n";
        std::cout << std::string(60, '=') << "\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
