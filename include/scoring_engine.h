#ifndef NEWSSCOPE_SCORING_ENGINE_H
#define NEWSSCOPE_SCORING_ENGINE_H

#include "types.h"
#include "preprocessing.h"
#include "source_validator.h"
#include "phrase_indexer.h"
#include "string_matcher.h"
#include "frequency_analyzer.h"
#include "temporal_analyzer.h"
#include "greedy_filter.h"
#include "claim_verifier.h"
#include <memory>
#include <chrono>
#include <mutex>
#include <string>

namespace newsscope {

class ScoringEngine {
public:
    ScoringEngine();
    
    void initialize(const std::string& sources_csv = "",
                   const std::string& suspicious_phrases_file = "",
                   const std::string& negative_terms_file = "");
    
    CredibilityResult assess_article(const Article& article);
    std::vector<CredibilityResult> assess_batch(const std::vector<Article>& articles);
    
    void set_module_weights(double preprocessing_weight, double source_weight,
                           double phrase_weight, double kmp_weight,
                           double rabin_karp_weight, double frequency_weight,
                           double temporal_weight, double greedy_weight);
    
    std::unordered_map<std::string, double> get_module_scores() const;
    std::vector<std::string> get_explanations() const;
    void reset();
    
    Preprocessor& get_preprocessor() { return *preprocessor; }
    SourceValidator& get_source_validator() { return *source_validator; }
    PhraseIndexer& get_phrase_indexer() { return *phrase_indexer; }
    FrequencyAnalyzer& get_frequency_analyzer() { return *frequency_analyzer; }
    TemporalAnalyzer& get_temporal_analyzer() { return *temporal_analyzer; }
    GreedyFilter& get_greedy_filter() { return *greedy_filter; }
    ClaimVerifier& get_claim_verifier() { return *claim_verifier; }
    
private:
    std::unordered_map<std::string, double> last_module_scores;
    std::vector<std::string> last_explanations;

    std::unique_ptr<Preprocessor> preprocessor;
    std::unique_ptr<SourceValidator> source_validator;
    std::unique_ptr<PhraseIndexer> phrase_indexer;
    std::unique_ptr<FrequencyAnalyzer> frequency_analyzer;
    std::unique_ptr<TemporalAnalyzer> temporal_analyzer;
    std::unique_ptr<GreedyFilter> greedy_filter;
    std::unique_ptr<ClaimVerifier> claim_verifier;
    
    static constexpr double DEFAULT_MODULE_WEIGHT = 12.5;  // 100 / 8 modules
    
    struct Weights {
        double preprocessing = DEFAULT_MODULE_WEIGHT;
        double source = DEFAULT_MODULE_WEIGHT;
        double phrase = DEFAULT_MODULE_WEIGHT;
        double kmp = DEFAULT_MODULE_WEIGHT;
        double rabin_karp = DEFAULT_MODULE_WEIGHT;
        double frequency = DEFAULT_MODULE_WEIGHT;
        double temporal = DEFAULT_MODULE_WEIGHT;
        double greedy = DEFAULT_MODULE_WEIGHT;
    } weights;
    
    mutable std::mutex assess_mutex;
    bool initialized_resources = false;

    bool ml_enabled = false;
    double ml_blend_weight = 0.0;
    std::string ml_model_path;
    std::string ml_tokenizer_path;
    std::string ml_inference_script_path;
    std::string ml_articles_path;
};

}

#endif
