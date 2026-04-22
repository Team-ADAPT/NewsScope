#include "greedy_filter.h"
#include <iostream>
#include <cassert>

using namespace newsscope;

void test_all_caps_detection() {
    std::cout << "Testing All Caps Detection...\n";
    GreedyFilter filter;
    
    std::string headline1 = "BREAKING NEWS!!!";
    auto signals1 = filter.detect_patterns(headline1);
    
    // Should detect all caps
    bool found_caps = false;
    for (const auto& signal : signals1) {
        if (signal.pattern_name == "ALL_CAPS") {
            found_caps = true;
            break;
        }
    }
    
    assert(found_caps == true);
    std::cout << "  ✓ All caps detection works\n";
}

void test_excessive_punctuation() {
    std::cout << "Testing Excessive Punctuation...\n";
    GreedyFilter filter;
    
    std::string headline = "You won't believe this!!!!!!";
    auto signals = filter.detect_patterns(headline);
    
    bool found_exclaim = false;
    for (const auto& signal : signals) {
        if (signal.pattern_name == "EXCESSIVE_EXCLAMATION") {
            found_exclaim = true;
            break;
        }
    }
    
    assert(found_exclaim == true);
    std::cout << "  ✓ Excessive exclamation detection works\n";
}

void test_sensational_words() {
    std::cout << "Testing Sensational Words...\n";
    GreedyFilter filter;
    
    std::string headline = "Absolutely shocking and unbelievable news!";
    auto signals = filter.detect_patterns(headline);
    
    bool found_sensational = false;
    for (const auto& signal : signals) {
        if (signal.pattern_name == "SENSATIONAL_WORDS") {
            found_sensational = true;
            break;
        }
    }
    
    assert(found_sensational == true);
    std::cout << "  ✓ Sensational words detection works\n";
}

void test_clickbait_structure() {
    std::cout << "Testing Clickbait Structure...\n";
    GreedyFilter filter;
    
    std::string headline = "You won't believe what happens next!";
    auto signals = filter.detect_patterns(headline);
    
    bool found_clickbait = false;
    for (const auto& signal : signals) {
        if (signal.pattern_name == "CLICKBAIT_STRUCTURE") {
            found_clickbait = true;
            break;
        }
    }
    
    assert(found_clickbait == true);
    std::cout << "  ✓ Clickbait structure detection works\n";
}

void test_urgency_tactics() {
    std::cout << "Testing Urgency Tactics...\n";
    GreedyFilter filter;
    
    std::string headline = "Act now and hurry! Limited time offer today!";
    auto signals = filter.detect_patterns(headline);
    
    bool found_urgency = false;
    for (const auto& signal : signals) {
        if (signal.pattern_name == "URGENCY_TACTICS") {
            found_urgency = true;
            break;
        }
    }
    
    assert(found_urgency == true);
    std::cout << "  ✓ Urgency tactics detection works\n";
}

void test_manipulation_score() {
    std::cout << "Testing Manipulation Score...\n";
    GreedyFilter filter;
    
    std::vector<GreedySignal> signals = {
        {"SIGNAL1", 0.8},
        {"SIGNAL2", 0.6},
        {"SIGNAL3", 0.5}
    };
    
    double score = filter.calculate_manipulation_score(signals);
    assert(score > 0.0 && score <= 100.0);
    
    std::cout << "  ✓ Manipulation score in valid range: " << score << "\n";
}

void test_empty_signals() {
    std::cout << "Testing Empty Signals...\n";
    GreedyFilter filter;
    
    std::vector<GreedySignal> signals;
    double score = filter.calculate_manipulation_score(signals);
    
    assert(score == 0.0);
    std::cout << "  ✓ Empty signals handled correctly\n";
}

void test_full_article_analysis() {
    std::cout << "Testing Full Article Analysis...\n";
    GreedyFilter filter;
    
    std::string headline = "SHOCKING NEWS!!!";
    std::string body = "This is OUTRAGEOUS and absolutely incredible!";
    
    double score = filter.analyze_article(headline, body);
    assert(score >= 0.0 && score <= 100.0);
    
    std::cout << "  ✓ Full article analysis score: " << score << "\n";
}

void test_non_clickbait_quote_is_not_flagged_as_clickbait() {
    std::cout << "Testing Non-Clickbait Quote Handling...\n";
    GreedyFilter filter;

    std::string headline = "Fed chair says 'you won't see immediate rate cuts'";
    auto signals = filter.detect_patterns(headline);

    bool found_clickbait = false;
    for (const auto& signal : signals) {
        if (signal.pattern_name == "CLICKBAIT_STRUCTURE") {
            found_clickbait = true;
            break;
        }
    }

    assert(!found_clickbait);
    std::cout << "  ✓ Ordinary quoted language no longer trips clickbait structure\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "GREEDY FILTER TESTS\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    try {
        test_all_caps_detection();
        test_excessive_punctuation();
        test_sensational_words();
        test_clickbait_structure();
        test_urgency_tactics();
        test_manipulation_score();
        test_empty_signals();
        test_full_article_analysis();
        test_non_clickbait_quote_is_not_flagged_as_clickbait();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "All greedy filter tests passed! ✓\n";
        std::cout << std::string(60, '=') << "\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
