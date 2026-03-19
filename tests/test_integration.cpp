#include "scoring_engine.h"
#include <iostream>
#include <cassert>
#include <iomanip>

using namespace newsscope;

void test_article_assessment() {
    std::cout << "Testing Article Assessment...\n";
    ScoringEngine engine;
    engine.initialize();
    
    Article article("test1", "Breaking News Title", "This is the body of the article.", "BBC");
    auto result = engine.assess_article(article);
    
    assert(result.overall_score >= 0.0 && result.overall_score <= 100.0);
    assert(result.module_scores.size() == 9);
    assert(!result.explanations.empty());
    
    std::cout << "  ✓ Overall score: " << result.overall_score << "/100\n";
    std::cout << "  ✓ Module scores counted: " << result.module_scores.size() << "\n";
}

void test_batch_assessment() {
    std::cout << "Testing Batch Assessment...\n";
    ScoringEngine engine;
    engine.initialize();
    
    std::vector<Article> articles = {
        Article("a1", "Title 1", "Body 1", "BBC"),
        Article("a2", "Title 2", "Body 2", "Reuters"),
        Article("a3", "Title 3", "Body 3", "Unknown")
    };
    
    auto results = engine.assess_batch(articles);
    
    assert(results.size() == 3);
    for (const auto& result : results) {
        assert(result.overall_score >= 0.0 && result.overall_score <= 100.0);
    }
    
    std::cout << "  ✓ Batch processing completed for " << results.size() << " articles\n";
}

void test_source_credibility_impact() {
    std::cout << "Testing Source Credibility Impact...\n";
    ScoringEngine engine;
    engine.initialize();
    
    Article trusted_article("t1", "News", "Body", "BBC");
    Article untrusted_article("u1", "News", "Body", "Fake News Daily");
    
    auto trusted_result = engine.assess_article(trusted_article);
    auto untrusted_result = engine.assess_article(untrusted_article);
    
    // Trusted source should generally score higher
    assert(trusted_result.overall_score > untrusted_result.overall_score + 10.0);
    
    std::cout << "  ✓ Trusted source score: " << trusted_result.overall_score << "\n";
    std::cout << "  ✓ Untrusted source score: " << untrusted_result.overall_score << "\n";
}

void test_default_initialize_loads_data_files() {
    std::cout << "Testing Default Data Loading...\n";
    ScoringEngine engine;
    engine.initialize();

    const double bloomberg_score = engine.get_source_validator().validate_source("Bloomberg");
    const double fake_daily_score = engine.get_source_validator().validate_source("Fake News Daily");

    assert(bloomberg_score >= 85.0);
    assert(fake_daily_score <= 20.0);
    std::cout << "  ✓ Default initialization loads source data: Bloomberg="
              << bloomberg_score << ", Fake News Daily=" << fake_daily_score << "\n";
}

void test_grounded_unknown_source_not_over_penalized() {
    std::cout << "Testing Grounded Unknown-Source Handling...\n";
    ScoringEngine engine;
    engine.initialize();

    Article grounded_unknown(
        "unknown-grounded",
        "Court issues interim order on refinery dispute",
        "According to the court filing, judges ordered both parties to submit audited emissions data within 14 days. "
        "The energy ministry said inspectors will publish the compliance summary next week.",
        "Unknown Source"
    );

    auto result = engine.assess_article(grounded_unknown);
    assert(result.overall_score >= 70.0);
    std::cout << "  ✓ Grounded unknown-source score: " << result.overall_score << "\n";
}

void test_uncertain_neutral_text_stays_midrange() {
    std::cout << "Testing Uncertain Neutral Text Calibration...\n";
    ScoringEngine engine;
    engine.initialize();

    Article neutral_article(
        "neutral-midrange",
        "Policy reset speculation grows",
        "Advisers close to the campaign say a policy reset is being drafted, but no written plan has been released. "
        "Analysts disagree on how meaningful the shift would be, and the campaign has not confirmed the reported details.",
        "Business Insider"
    );

    auto result = engine.assess_article(neutral_article);
    assert(result.overall_score >= 55.0 && result.overall_score <= 85.0);
    std::cout << "  ✓ Uncertain neutral score: " << result.overall_score << "\n";
}

void test_extraordinary_unknown_source_claim_is_penalized() {
    std::cout << "Testing Extraordinary Unknown-Source Claim Penalty...\n";
    ScoringEngine engine;
    engine.initialize();

    Article extraordinary_claim(
        "extraordinary-claim",
        "Researchers claim to have discovered a new element capable of producing unlimited energy without any environmental impact.",
        "The breakthrough is being hailed as a revolutionary step toward solving global energy challenges. "
        "However, the findings have not yet been peer-reviewed, and detailed technical reports are still pending.",
        "Unknown Source"
    );

    auto result = engine.assess_article(extraordinary_claim);
    assert(result.module_scores.at("claim_verifiability") < 50.0);
    assert(result.overall_score < 68.0);
    std::cout << "  ✓ Extraordinary unsupported claim score: " << result.overall_score << "\n";
}

void test_clickbait_detection_impact() {
    std::cout << "Testing Clickbait Detection Impact...\n";
    ScoringEngine engine;
    engine.initialize();
    
    Article neutral_article("n1", "Normal News Title", "This is factual content.", "BBC");
    Article clickbait_article("c1", "SHOCKING!!! You Won't BELIEVE!!!",
                             "OUTRAGEOUS and ABSOLUTELY incredible!!!",
                             "BBC");
    
    auto neutral_result = engine.assess_article(neutral_article);
    auto clickbait_result = engine.assess_article(clickbait_article);
    
    // Neutral should score higher than clickbait
    assert(neutral_result.overall_score >= clickbait_result.overall_score);
    
    std::cout << "  ✓ Neutral article score: " << neutral_result.overall_score << "\n";
    std::cout << "  ✓ Clickbait article score: " << clickbait_result.overall_score << "\n";
}

void test_polished_misinformation_penalty() {
    std::cout << "Testing Polished Misinformation Penalty...\n";
    ScoringEngine engine;
    engine.initialize();

    Article trusted_factual(
        "factual",
        "Peer-reviewed climate study released",
        "According to an official report, independent audits and documented evidence confirm the findings.",
        "Reuters"
    );

    Article polished_misinfo(
        "misinfo",
        "Emergency funds transparency update",
        "Social media posts claim the process was fully transparent, but audits are not yet published and no official confirmation exists.",
        "Unknown Source"
    );

    auto factual_result = engine.assess_article(trusted_factual);
    auto misinfo_result = engine.assess_article(polished_misinfo);

    assert(factual_result.overall_score > misinfo_result.overall_score);
    assert(factual_result.overall_score >= 90.0);  // strong trusted+factual article
    std::cout << "  ✓ Trusted factual score: " << factual_result.overall_score << "\n";
    std::cout << "  ✓ Polished misinformation score: " << misinfo_result.overall_score << "\n";
}

void test_concise_real_news_not_over_penalized() {
    std::cout << "Testing Concise Real-News Handling...\n";
    ScoringEngine engine;
    engine.initialize();

    Article concise_real(
        "real-geo",
        "Energy route tensions increase",
        "Iran's attacks on Gulf nations and its grip on the Strait of Hormuz, through which a fifth of the world's oil is transported, have sparked increasing concerns of a global energy crisis and are unnerving the world economy.",
        "Unknown Source"
    );

    auto result = engine.assess_article(concise_real);
    assert(result.overall_score >= 50.0);
    std::cout << "  ✓ Concise real-like score: " << result.overall_score << "\n";
}

void test_module_scores() {
    std::cout << "Testing Module Scores...\n";
    ScoringEngine engine;
    engine.initialize();
    
    Article article("test", "Title", "Body", "BBC");
    auto result = engine.assess_article(article);
    
    for (const auto& [module, score] : result.module_scores) {
        assert(score >= 0.0 && score <= 100.0);
    }
    
    std::cout << "  ✓ All module scores in valid range (0-100)\n";
    std::cout << "  ✓ Modules: ";
    for (const auto& [module, score] : result.module_scores) {
        std::cout << module << " ";
    }
    std::cout << "\n";
}

void test_custom_weights() {
    std::cout << "Testing Custom Weights...\n";
    ScoringEngine engine;
    engine.initialize();
    
    // Set custom weights
    engine.set_module_weights(
        50.0,   // preprocessing
        50.0,   // source
        0.0,    // phrase
        0.0,    // kmp
        0.0,    // rabin_karp
        0.0,    // frequency
        0.0,    // temporal
        0.0     // greedy
    );
    
    Article article("test", "News", "Body", "BBC");
    auto result = engine.assess_article(article);
    
    // With only preprocessing and source weights, score should be average of those two
    std::cout << "  ✓ Custom weights applied, score: " << result.overall_score << "\n";
}

void test_explanations() {
    std::cout << "Testing Explanations...\n";
    ScoringEngine engine;
    engine.initialize();
    
    Article article("test", "Title", "Body", "BBC");
    auto result = engine.assess_article(article);
    
    assert(!result.explanations.empty());
    
    std::cout << "  ✓ Generated " << result.explanations.size() << " explanations\n";
    for (size_t i = 0; i < std::min(size_t(3), result.explanations.size()); ++i) {
        std::cout << "    - " << result.explanations[i] << "\n";
    }
}

void test_processing_time() {
    std::cout << "Testing Processing Time...\n";
    ScoringEngine engine;
    engine.initialize();
    
    Article article("test", "Title", "Body text here", "BBC");
    auto result = engine.assess_article(article);
    
    assert(result.processing_time.count() >= 0);
    
    std::cout << "  ✓ Processing time: " << result.processing_time.count() << " ms\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "INTEGRATION TESTS\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    try {
        test_article_assessment();
        test_batch_assessment();
        test_source_credibility_impact();
        test_default_initialize_loads_data_files();
        test_clickbait_detection_impact();
        test_polished_misinformation_penalty();
        test_concise_real_news_not_over_penalized();
        test_grounded_unknown_source_not_over_penalized();
        test_uncertain_neutral_text_stays_midrange();
        test_extraordinary_unknown_source_claim_is_penalized();
        test_module_scores();
        test_custom_weights();
        test_explanations();
        test_processing_time();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "All integration tests passed! ✓\n";
        std::cout << std::string(60, '=') << "\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
