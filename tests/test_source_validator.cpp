#include "source_validator.h"
#include <iostream>
#include <cassert>

using namespace newsscope;

void test_trusted_source() {
    std::cout << "Testing Trusted Source...\n";
    SourceValidator validator;
    
    double score = validator.validate_source("BBC");
    assert(score >= 70.0);  // BBC should have high credibility
    
    std::cout << "  ✓ BBC score: " << score << "/100\n";
}

void test_untrusted_source() {
    std::cout << "Testing Untrusted Source...\n";
    SourceValidator validator;
    
    double score = validator.validate_source("Fake News Daily");
    assert(score < 50.0);  // Fake source should have low credibility
    
    std::cout << "  ✓ Fake News Daily score: " << score << "/100\n";
}

void test_unknown_source() {
    std::cout << "Testing Unknown Source...\n";
    SourceValidator validator;
    
    double score = validator.validate_source("UnknownNews.com");
    assert(score == 50.0);  // Unknown source is treated as neutral, not automatically suspicious
    
    std::cout << "  ✓ Unknown source score: " << score << "/100\n";
}

void test_domain_like_source_lookup() {
    std::cout << "Testing Domain-like Source Lookup...\n";
    SourceValidator validator;

    const double canonical = validator.validate_source("Reuters");
    const double domain_like = validator.validate_source("www.reuters.com");

    assert(canonical == domain_like);
    std::cout << "  ✓ Domain-like source aliases normalize correctly\n";
}

void test_subdomain_source_lookup() {
    std::cout << "Testing Subdomain Source Lookup...\n";
    SourceValidator validator;

    const double reuters = validator.validate_source("Reuters");
    const double mobile_reuters = validator.validate_source("mobile.reuters.com");
    const double amp_nyt = validator.validate_source("amp.nytimes.com");

    assert(reuters == mobile_reuters);
    assert(amp_nyt >= 90.0);
    std::cout << "  ✓ Common mobile/amp publisher aliases normalize correctly\n";
}

void test_news_suffix_is_not_stripped_for_named_publishers() {
    std::cout << "Testing Publisher Name Collision Handling...\n";
    SourceValidator validator;

    validator.add_trusted_source("ABC News", 87.0);
    validator.add_trusted_source("ABC Australia", 90.0);

    const double abc_news = validator.validate_source("ABC News");
    const double abc_australia = validator.validate_source("ABC Australia");

    assert(abc_news == 87.0);
    assert(abc_australia == 90.0);
    std::cout << "  ✓ Distinct publishers no longer collapse into one source key\n";
}

void test_add_source() {
    std::cout << "Testing Add Source...\n";
    SourceValidator validator;
    
    validator.add_trusted_source("MyNews", 75.0);
    double score = validator.validate_source("MyNews");
    
    assert(score == 75.0);
    
    std::cout << "  ✓ Custom source added with score: " << score << "/100\n";
}

void test_source_status() {
    std::cout << "Testing Source Status...\n";
    SourceValidator validator;
    
    auto status = validator.get_source_status("BBC");
    assert(status == SourceValidator::SourceStatus::TRUSTED);
    
    status = validator.get_source_status("Fake News Daily");
    assert(status == SourceValidator::SourceStatus::UNTRUSTED);
    
    std::cout << "  ✓ Source status detection works correctly\n";
}

void test_source_exists() {
    std::cout << "Testing Source Exists...\n";
    SourceValidator validator;
    
    assert(validator.source_exists("BBC") == true);
    assert(validator.source_exists("NonExistent") == false);
    
    std::cout << "  ✓ Source existence check works correctly\n";
}

void test_case_insensitive_lookup() {
    std::cout << "Testing Case-Insensitive Source Lookup...\n";
    SourceValidator validator;

    const double canonical = validator.validate_source("Reuters");
    const double lowercase = validator.validate_source("reuters");
    const double mixed = validator.validate_source(" ReUtErS ");

    assert(canonical == lowercase);
    assert(canonical == mixed);
    std::cout << "  ✓ Case-insensitive lookup works correctly\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "SOURCE VALIDATOR TESTS\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    try {
        test_trusted_source();
        test_untrusted_source();
        test_unknown_source();
        test_add_source();
        test_source_status();
        test_source_exists();
        test_case_insensitive_lookup();
        test_domain_like_source_lookup();
        test_subdomain_source_lookup();
        test_news_suffix_is_not_stripped_for_named_publishers();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "All source validator tests passed! ✓\n";
        std::cout << std::string(60, '=') << "\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
