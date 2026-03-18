#include "preprocessing.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

using namespace newsscope;

void test_tokenization() {
    std::cout << "Testing Tokenization...\n";
    Preprocessor prep;
    
    std::string text = "Hello, World! This is a test.";
    auto tokens = prep.tokenize(text);
    
    assert(tokens.size() > 0);
    assert(tokens[0] == "hello");
    assert(tokens[1] == "world");
    
    std::cout << "  ✓ Tokenization works correctly\n";
}

void test_stop_word_removal() {
    std::cout << "Testing Stop Word Removal...\n";
    Preprocessor prep;
    
    std::vector<std::string> tokens = {"the", "quick", "brown", "fox", "jumps"};
    prep.remove_stop_words(tokens);
    
    assert(tokens.size() == 4);  // "the" should be removed
    
    std::cout << "  ✓ Stop word removal works correctly\n";
}

void test_normalization() {
    std::cout << "Testing Normalization...\n";
    Preprocessor prep;
    
    std::string token = "HeLLo@123!";
    std::string normalized = prep.normalize_token(token);
    
    assert(normalized == "hello123");
    
    std::cout << "  ✓ Normalization works correctly\n";
}

void test_article_processing() {
    std::cout << "Testing Article Processing...\n";
    Preprocessor prep;
    
    Article article("id1", "Breaking News", "This is the body of the article.", "BBC");
    auto tokens = prep.process(article);
    
    assert(tokens.size() > 0);
    
    std::cout << "  ✓ Article processing works correctly\n";
}

void test_custom_stop_words() {
    std::cout << "Testing Custom Stop Words...\n";
    Preprocessor prep;
    
    prep.add_stop_word("custom");
    std::vector<std::string> tokens = {"hello", "custom", "world"};
    prep.remove_stop_words(tokens);
    
    assert(tokens.size() == 2);
    
    std::cout << "  ✓ Custom stop words work correctly\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "PREPROCESSING MODULE TESTS\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    try {
        test_tokenization();
        test_stop_word_removal();
        test_normalization();
        test_article_processing();
        test_custom_stop_words();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "All preprocessing tests passed! ✓\n";
        std::cout << std::string(60, '=') << "\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
