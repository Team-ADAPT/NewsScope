#include "scoring_engine.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace newsscope;

int main() {
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "NewsScope: Latency Benchmark\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    ScoringEngine engine;
    engine.initialize();
    
    // Create test articles of varying sizes
    struct TestCase {
        std::string name;
        int body_size;  // Approximate size in characters
        int count;      // Number of articles to test
    };
    
    std::vector<TestCase> test_cases = {
        {"Small (100 chars)", 100, 100},
        {"Medium (500 chars)", 500, 100},
        {"Large (2000 chars)", 2000, 100},
        {"Very Large (5000 chars)", 5000, 50}
    };
    
    for (const auto& test_case : test_cases) {
        std::cout << "\nTest Case: " << test_case.name << "\n";
        std::cout << std::string(80, '-') << "\n";
        
        // Create test articles
        std::string body_template = "This is test content about news articles and credibility assessment. ";
        std::string body = "";
        while (body.length() < static_cast<size_t>(test_case.body_size)) {
            body += body_template;
        }
        
        std::vector<double> latencies_ms;
        
        for (int i = 0; i < test_case.count; ++i) {
            Article article(
                "article_" + std::to_string(i),
                "Test Article Headline " + std::to_string(i),
                body.substr(0, test_case.body_size),
                (i % 3 == 0) ? "BBC" : (i % 3 == 1) ? "Reuters" : "Unknown"
            );
            
            auto start = std::chrono::high_resolution_clock::now();
            auto result = engine.assess_article(article);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto latency = std::chrono::duration<double, std::milli>(end - start).count();
            latencies_ms.push_back(latency);
        }
        
        // Calculate statistics
        std::sort(latencies_ms.begin(), latencies_ms.end());
        
        double sum = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0);
        double avg = sum / latencies_ms.size();
        double min = *std::min_element(latencies_ms.begin(), latencies_ms.end());
        double max = *std::max_element(latencies_ms.begin(), latencies_ms.end());
        double p50 = latencies_ms[latencies_ms.size() / 2];
        size_t p99_index = static_cast<size_t>(0.99 * static_cast<double>(latencies_ms.size() - 1));
        double p99 = latencies_ms[p99_index];
        
        std::cout << std::fixed << std::setprecision(3)
                  << "Min:     " << min << " ms\n"
                  << "Max:     " << max << " ms\n"
                  << "Avg:     " << avg << " ms\n"
                  << "P50:     " << p50 << " ms\n"
                  << "P99:     " << p99 << " ms\n";
    }
    
    // Benchmark: Module-specific latencies
    std::cout << "\n\nModule-Specific Latency Analysis\n";
    std::cout << std::string(80, '-') << "\n";
    
    Article test_article(
        "latency_test",
        "Complex Headline With Multiple Suspicious Elements",
        std::string("This is a detailed test body with complex content. ") +
        "It mentions fake news and conspiracy theories. " +
        "SHOCKING DISCOVERY EXPOSED!!! " +
        "This absolutely unbelievable situation has outraged citizens everywhere!",
        "Test Source"
    );
    
    std::cout << "\nTesting 10 iterations...\n";
    
    std::vector<double> total_latencies_ms;
    
    for (int i = 0; i < 10; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        auto result = engine.assess_article(test_article);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto latency = std::chrono::duration<double, std::milli>(end - start).count();
        
        total_latencies_ms.push_back(latency);
        
        std::cout << "  Iteration " << (i + 1) << ": " << latency << " ms\n";
    }
    
    // Summary
    std::cout << "\nLatency Summary:\n";
    double avg_total = std::accumulate(total_latencies_ms.begin(), total_latencies_ms.end(), 0.0)
                     / total_latencies_ms.size();
    std::cout << "Average latency: " << std::fixed << std::setprecision(3)
              << avg_total << " ms\n";
    
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "Latency Benchmark Complete!\n";
    std::cout << "\nTarget Performance:\n";
    std::cout << "  P50 Latency: <= 10 ms\n";
    std::cout << "  P99 Latency: <= 50 ms\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    return 0;
}
