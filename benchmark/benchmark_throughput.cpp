#include "scoring_engine.h"
#include "thread_pool.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>

using namespace newsscope;

int main() {
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "NewsScope: Throughput Benchmark\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    ScoringEngine engine;
    engine.initialize();
    
    // Create test articles
    std::vector<Article> articles;
    for (int i = 0; i < 1000; ++i) {
        articles.push_back(Article(
            "article_" + std::to_string(i),
            "Test Headline " + std::to_string(i),
            std::string("This is a test article body with some content to process. ") +
            "It contains multiple sentences for comprehensive analysis. ",
            (i % 2 == 0) ? "BBC" : "Unknown Source"
        ));
    }
    
    std::cout << "Testing with " << articles.size() << " articles\n\n";
    
    // Benchmark 1: Sequential processing
    std::cout << "1. SEQUENTIAL PROCESSING\n";
    std::cout << std::string(80, '-') << "\n";
    
    auto seq_start = std::chrono::high_resolution_clock::now();
    auto seq_results = engine.assess_batch(articles);
    auto seq_end = std::chrono::high_resolution_clock::now();
    
    auto seq_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        seq_end - seq_start
    );
    
    double seq_throughput = (static_cast<double>(articles.size()) / static_cast<double>(seq_duration.count())) * 1000.0;
    
    std::cout << "Total Time: " << seq_duration.count() << " ms\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) 
              << seq_throughput << " articles/sec\n";
    std::cout << "Avg per article: " << (seq_duration.count() / (double)articles.size())
              << " ms\n\n";
    
    // Benchmark 2: Concurrent processing with thread pool
    std::cout << "2. CONCURRENT PROCESSING (Thread Pool - 8 threads)\n";
    std::cout << std::string(80, '-') << "\n";
    
    ThreadPool pool(8);
    
    auto concurrent_start = std::chrono::high_resolution_clock::now();
    
    int completed = 0;
    std::mutex result_mutex;
    
    for (const auto& article : articles) {
        pool.enqueue([&article, &completed, &result_mutex]() {
            thread_local ScoringEngine local_engine;
            thread_local bool initialized = false;
            if (!initialized) {
                local_engine.initialize();
                initialized = true;
            }
            auto result = local_engine.assess_article(article);
            {
                std::lock_guard<std::mutex> lock(result_mutex);
                completed++;
            }
        });
    }
    
    pool.wait_for_all();
    
    auto concurrent_end = std::chrono::high_resolution_clock::now();
    
    auto concurrent_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        concurrent_end - concurrent_start
    );
    
    double concurrent_throughput = (static_cast<double>(articles.size()) / static_cast<double>(concurrent_duration.count())) * 1000.0;
    
    std::cout << "Total Time: " << concurrent_duration.count() << " ms\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(2)
              << concurrent_throughput << " articles/sec\n";
    std::cout << "Speedup: " << (seq_duration.count() / (double)concurrent_duration.count())
              << "x\n";
    std::cout << "Completed Articles: " << completed << "\n\n";
    
    // Benchmark 3: Different pool sizes
    std::cout << "3. THROUGHPUT vs THREAD POOL SIZE\n";
    std::cout << std::string(80, '-') << "\n";
    std::cout << std::left << std::setw(20) << "Thread Count"
              << std::setw(20) << "Time (ms)"
              << std::setw(20) << "Throughput (art/s)\n";
    std::cout << std::string(60, '-') << "\n";
    
    for (int thread_count : {1, 2, 4, 8, 16, 32}) {
        ThreadPool test_pool(thread_count);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (const auto& article : articles) {
            test_pool.enqueue([&article]() {
                thread_local ScoringEngine local_engine;
                thread_local bool initialized = false;
                if (!initialized) {
                    local_engine.initialize();
                    initialized = true;
                }
                local_engine.assess_article(article);
            });
        }
        
        test_pool.wait_for_all();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        double throughput = (static_cast<double>(articles.size()) / static_cast<double>(duration.count())) * 1000.0;
        
        std::cout << std::left << std::setw(20) << thread_count
                  << std::setw(20) << duration.count()
                  << std::fixed << std::setprecision(2) << std::setw(20) << throughput << "\n";
    }
    
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "Benchmark Complete!\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    return 0;
}
