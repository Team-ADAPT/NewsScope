#include "phrase_indexer.h"
#include <iostream>
#include <cassert>

using namespace newsscope;

void test_insert_and_search() {
    std::cout << "Testing Insert and Search...\n";
    PhraseIndexer indexer;
    
    indexer.insert_phrase("fake news");
    indexer.insert_phrase("conspiracy theory");
    
    assert(indexer.phrase_exists("fake news") == true);
    assert(indexer.phrase_exists("hoax") == false);
    
    std::cout << "  ✓ Insert and search work correctly\n";
}

void test_prefix_search() {
    std::cout << "Testing Prefix Search...\n";
    PhraseIndexer indexer;
    
    indexer.insert_phrase("fake news");
    indexer.insert_phrase("fake report");
    indexer.insert_phrase("real news");
    
    auto results = indexer.find_by_prefix("fake");
    assert(results.size() == 2);
    
    std::cout << "  ✓ Prefix search found " << results.size() << " phrases\n";
}

void test_phrase_in_text() {
    std::cout << "Testing Find Phrases in Text...\n";
    PhraseIndexer indexer;
    
    indexer.insert_phrase("fake news");
    indexer.insert_phrase("conspiracy");
    
    std::string text = "This is fake news about a conspiracy theory.";
    auto found = indexer.find_in_text(text);
    
    assert(found.size() >= 1);  // Should find at least "fake news" or "conspiracy"
    
    std::cout << "  ✓ Found " << found.size() << " phrase(s) in text\n";
}

void test_phrase_count() {
    std::cout << "Testing Phrase Count...\n";
    PhraseIndexer indexer;
    
    indexer.insert_phrase("phrase1");
    indexer.insert_phrase("phrase2");
    indexer.insert_phrase("phrase3");
    
    assert(indexer.phrase_count() == 3);
    
    std::cout << "  ✓ Phrase count: " << indexer.phrase_count() << "\n";
}

void test_duplicate_insertion() {
    std::cout << "Testing Duplicate Insertion...\n";
    PhraseIndexer indexer;
    
    indexer.insert_phrase("duplicate");
    indexer.insert_phrase("duplicate");  // Insert same phrase again
    
    assert(indexer.phrase_count() == 1);  // Count should remain 1
    
    std::cout << "  ✓ Duplicate insertion handled correctly\n";
}

void test_clear() {
    std::cout << "Testing Clear...\n";
    PhraseIndexer indexer;
    
    indexer.insert_phrase("test1");
    indexer.insert_phrase("test2");
    indexer.clear();
    
    assert(indexer.phrase_count() == 0);
    
    std::cout << "  ✓ Clear works correctly\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "PHRASE INDEXER (TRIE) TESTS\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    try {
        test_insert_and_search();
        test_prefix_search();
        test_phrase_in_text();
        test_phrase_count();
        test_duplicate_insertion();
        test_clear();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "All phrase indexer tests passed! ✓\n";
        std::cout << std::string(60, '=') << "\n\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
