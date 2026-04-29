#include "scoring_engine.h"
#include "thread_pool.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

using namespace newsscope;

namespace {

std::string resolve_data_file(const std::string& filename) {
    const std::vector<std::string> candidates = {
        "data/" + filename,
        "../data/" + filename,
        "../../data/" + filename
    };

    for (const auto& path : candidates) {
        std::ifstream file(path);
        if (file.is_open()) {
            return path;
        }
    }
    return "";
}

void skip_whitespace(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

bool parse_json_string(const std::string& text, size_t& pos, std::string& out) {
    if (pos >= text.size() || text[pos] != '"') {
        return false;
    }
    ++pos;
    out.clear();

    while (pos < text.size()) {
        const char c = text[pos++];
        if (c == '"') {
            return true;
        }
        if (c == '\\') {
            if (pos >= text.size()) {
                return false;
            }
            const char esc = text[pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                    // Skip unicode escape hex digits and keep placeholder.
                    for (int i = 0; i < 4 && pos < text.size(); ++i) {
                        ++pos;
                    }
                    out.push_back('?');
                    break;
                default:
                    out.push_back(esc);
                    break;
            }
            continue;
        }
        out.push_back(c);
    }
    return false;
}

bool skip_json_value(const std::string& text, size_t& pos) {
    skip_whitespace(text, pos);
    if (pos >= text.size()) {
        return false;
    }

    if (text[pos] == '"') {
        std::string ignored;
        return parse_json_string(text, pos, ignored);
    }

    if (text[pos] == '{' || text[pos] == '[') {
        const char open = text[pos];
        const char close = (open == '{') ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        while (pos < text.size()) {
            const char c = text[pos++];
            if (in_string) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    in_string = false;
                }
                continue;
            }
            if (c == '"') {
                in_string = true;
                continue;
            }
            if (c == open) {
                ++depth;
            } else if (c == close) {
                --depth;
                if (depth == 0) {
                    return true;
                }
            }
        }
        return false;
    }

    while (pos < text.size()) {
        const char c = text[pos];
        if (c == ',' || c == '}' || c == ']') {
            return true;
        }
        ++pos;
    }
    return true;
}

std::vector<Article> load_articles_from_json(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    std::vector<Article> articles;
    articles.reserve(static_cast<size_t>(std::count(text.begin(), text.end(), '{')));

    size_t pos = 0;
    skip_whitespace(text, pos);
    if (pos >= text.size() || text[pos] != '[') {
        return {};
    }
    ++pos;

    while (pos < text.size()) {
        skip_whitespace(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            break;
        }
        if (pos >= text.size() || text[pos] != '{') {
            break;
        }
        ++pos;

        std::string id;
        std::string headline;
        std::string body;
        std::string source;

        while (pos < text.size()) {
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == '}') {
                ++pos;
                break;
            }

            std::string key;
            if (!parse_json_string(text, pos, key)) {
                return articles;
            }
            skip_whitespace(text, pos);
            if (pos >= text.size() || text[pos] != ':') {
                return articles;
            }
            ++pos;
            skip_whitespace(text, pos);

            std::string value;
            if (pos < text.size() && text[pos] == '"') {
                if (!parse_json_string(text, pos, value)) {
                    return articles;
                }
            } else {
                if (!skip_json_value(text, pos)) {
                    return articles;
                }
            }

            if (key == "id") {
                id = std::move(value);
            } else if (key == "headline") {
                headline = std::move(value);
            } else if (key == "body") {
                body = std::move(value);
            } else if (key == "source") {
                source = std::move(value);
            }

            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == ',') {
                ++pos;
                continue;
            }
        }

        if (!headline.empty() && !body.empty()) {
            if (id.empty()) {
                id = "article-" + std::to_string(articles.size() + 1);
            }
            if (source.empty()) {
                source = "Unknown Source";
            }
            articles.emplace_back(id, headline, body, source);
        }

        skip_whitespace(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }

    return articles;
}

std::vector<Article> get_fallback_articles() {
    std::vector<Article> articles;
    articles.reserve(3);
    articles.emplace_back(
        "article_1",
        "Breaking: New Research Reveals Cancer Treatment Breakthrough",
        "Researchers at Stanford University have announced a significant breakthrough in cancer treatment. "
        "The study, published in Nature Journal, shows promising results from a five-year clinical trial. "
        "The research was conducted by Dr. Jane Smith and her team over the past decade. "
        "The findings will be presented at the International Medical Conference next month.",
        "BBC"
    );
    articles.emplace_back(
        "article_2",
        "SHOCKING!!!  You Won't BELIEVE What Doctors Don't Want You To Know!!!",
        "This ONE TRICK will change your LIFE FOREVER! Big Pharma companies are EXPOSED!!! "
        "Celebrities are FURIOUS!!! Click here NOW before this gets taken down!!! "
        "URGENT: Limited time offer - Act NOW!!!",
        "Fake News Daily"
    );
    articles.emplace_back(
        "article_3",
        "Local Community Faces Housing Crisis As Prices Surge",
        "Local real estate prices have reached unprecedented levels this quarter. "
        "The city council meeting revealed shocking statistics about affordability. "
        "Experts exposed the corruption in zoning regulations. "
        "The scandal has outraged community members. However, credible sources confirm improvements are planned.",
        "Local News Today"
    );
    return articles;
}

}  // namespace

void print_result(const CredibilityResult& result, const std::string& article_id) {
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "Article ID: " << article_id << "\n";
    std::cout << std::string(80, '=') << "\n";
    
    std::cout << "Overall Credibility Score: " << std::fixed << std::setprecision(2)
              << result.overall_score << "/100\n\n";
    std::cout << "Deterministic Score: " << result.deterministic_score << "/100\n";
    if (result.ml_score >= 0.0) {
        std::cout << "ML Model Score: " << result.ml_score << "/100\n";
    }
    std::cout << "\n";
    
    std::cout << "Module Scores:\n";
    for (const auto& [module, score] : result.module_scores) {
        std::cout << "  " << std::left << std::setw(25) << module << ": "
                  << std::fixed << std::setprecision(2) << score << "/100\n";
    }
    
    std::cout << "\nDetailed Explanation:\n";
    for (const auto& explanation : result.explanations) {
        std::cout << "  - " << explanation << "\n";
    }
    
    std::cout << "Processing Time: " << result.processing_time.count() << " ms\n";
}

int main() {
    std::cout << "\n" << std::string(80, '*') << "\n";
    std::cout << "NewsScope: Scalable News Credibility Assessment System\n";
    std::cout << std::string(80, '*') << "\n\n";
    
    // Initialize engine with data files when available.
    ScoringEngine engine;
    const std::string sources_path = resolve_data_file("sources.csv");
    const std::string phrases_path = resolve_data_file("suspicious_phrases.txt");
    const std::string negative_terms_path = resolve_data_file("negative_terms.csv");
    engine.initialize(sources_path, phrases_path, negative_terms_path);

    std::vector<Article> articles;
    const std::string articles_path = resolve_data_file("articles.json");
    if (!articles_path.empty()) {
        articles = load_articles_from_json(articles_path);
    }
    if (articles.empty()) {
        articles = get_fallback_articles();
        std::cout << "Using built-in sample articles (articles.json not available or invalid).\n";
    } else {
        std::cout << "Loaded " << articles.size() << " articles from " << articles_path << "\n";
    }
    
    // Single article assessment
    std::cout << "=== SINGLE ARTICLE ASSESSMENT ===\n";
    const size_t single_article_count = std::min<size_t>(3, articles.size());
    for (size_t i = 0; i < single_article_count; ++i) {
        auto result = engine.assess_article(articles[i]);
        print_result(result, articles[i].id);
    }
    
    // Batch assessment demonstration
    std::cout << "\n\n=== BATCH PROCESSING DEMONSTRATION ===\n";
    std::vector<Article> batch_articles;
    if (articles.size() > 20) {
        int trusted_count = 0;
        int fake_count = 0;
        int other_count = 0;
        for (const auto& a : articles) {
            if (a.id.find("trusted-") != std::string::npos) {
                if (trusted_count < 7) {
                    batch_articles.push_back(a);
                    trusted_count++;
                }
            } else if (a.id.find("fake-") != std::string::npos) {
                if (fake_count < 7) {
                    batch_articles.push_back(a);
                    fake_count++;
                }
            } else {
                if (other_count < 6) {
                    batch_articles.push_back(a);
                    other_count++;
                }
            }
            if (batch_articles.size() >= 20) break;
        }
        // Fill up if we didn't find exactly 20
        for (const auto& a : articles) {
            if (batch_articles.size() >= 20) break;
            bool found = false;
            for (const auto& ba : batch_articles) {
                if (ba.id == a.id) { found = true; break; }
            }
            if (!found) batch_articles.push_back(a);
        }
    } else {
        batch_articles = articles;
    }
    auto batch_results = engine.assess_batch(batch_articles);
    
    std::cout << "\nBatch Processing Summary:\n";
    std::cout << std::left << std::setw(15) << "Article ID"
              << std::setw(20) << "Credibility Score"
              << std::setw(20) << "Processing Time (ms)\n";
    std::cout << std::string(55, '-') << "\n";
    
    for (size_t i = 0; i < batch_results.size(); ++i) {
        std::cout << std::left << std::setw(15) << batch_articles[i].id
                  << std::fixed << std::setprecision(2) << std::setw(20) << batch_results[i].overall_score
                  << std::setw(20) << batch_results[i].processing_time.count() << "\n";
    }
    
    // Thread pool demonstration
    std::cout << "\n\n=== CONCURRENT PROCESSING (Thread Pool) ===\n";
    ThreadPool pool(4);  // 4 worker threads
    
    std::vector<Article> concurrent_articles = batch_articles;
    if (concurrent_articles.size() > 8) {
        concurrent_articles.resize(8);
    }
    std::cout << "Processing " << concurrent_articles.size() << " articles concurrently...\n";
    
    auto concurrent_start = std::chrono::high_resolution_clock::now();
    
    for (const auto& article : concurrent_articles) {
        pool.enqueue([&engine, &article]() {
            auto result = engine.assess_article(article);
            std::cout << "  Processed: " << article.id << " -> Score: "
                      << std::fixed << std::setprecision(2) << result.overall_score << "\n";
        });
    }
    
    pool.wait_for_all();
    
    auto concurrent_end = std::chrono::high_resolution_clock::now();
    auto concurrent_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        concurrent_end - concurrent_start
    );
    
    std::cout << "\nConcurrent processing completed in " << concurrent_duration.count() << " ms\n";
    
    // Score interpretation guide
    std::cout << "\n\n=== CREDIBILITY SCORE INTERPRETATION ===\n";
    std::cout << "  90-100: Very High Credibility - Trusted source, factual content\n";
    std::cout << "  70-89:  High Credibility - Generally reliable with some concerns\n";
    std::cout << "  50-69:  Medium Credibility - Mixed signals, needs verification\n";
    std::cout << "  30-49:  Low Credibility - Multiple red flags detected\n";
    std::cout << "  0-29:   Very Low Credibility - High likelihood of misinformation\n";
    
    std::cout << "\n" << std::string(80, '*') << "\n";
    std::cout << "Demo Complete!\n";
    std::cout << std::string(80, '*') << "\n\n";
    
    return 0;
}
