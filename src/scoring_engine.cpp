#include "scoring_engine.h"
#include "utils.h"
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <cmath>

namespace {

// =============================================================================
// SCORING CONSTANTS
// =============================================================================

// Base scores
constexpr double BASE_SCORE = 85.0;
constexpr double BASELINE_PREPROCESSING_SCORE = 70.0;  // neutral articles start here

// Short text handling
constexpr size_t SHORT_TEXT_THRESHOLD_CRITICAL = 5;
constexpr size_t SHORT_TEXT_THRESHOLD_WARNING = 12;
constexpr double SHORT_TEXT_PENALTY_CRITICAL = 7.0;
constexpr double SHORT_TEXT_PENALTY_WARNING = 2.5;

// Factual/uncertainty cue scoring — capped to avoid single-phrase dominance
constexpr double FACTUAL_CUE_BONUS = 3.5;
constexpr double UNCERTAINTY_PENALTY = 4.5;

// Pattern matching penalties
constexpr double PHRASE_PENALTY_PER_HIT = 9.0;
constexpr double KMP_PENALTY_PER_HIT = 6.5;
constexpr double RABIN_KARP_PENALTY_PER_HIT = 4.5;
constexpr double FREQUENCY_PENALTY_MULTIPLIER = 0.50;

// Maximum penalties per module
constexpr double MAX_PHRASE_PENALTY = 27.0;
constexpr double MAX_KMP_PENALTY = 26.0;
constexpr double MAX_RABIN_KARP_PENALTY = 18.0;
constexpr double MAX_FREQUENCY_PENALTY = 32.0;

// Risk assessment thresholds
constexpr double VERY_LOW_SOURCE_THRESHOLD = 20.0;
constexpr double LOW_SOURCE_CREDIBILITY_THRESHOLD = 35.0;
constexpr double MEDIUM_SOURCE_CREDIBILITY_THRESHOLD = 50.0;
constexpr double HIGH_SOURCE_THRESHOLD = 75.0;
constexpr double LOW_CLAIM_VERIFIABILITY_THRESHOLD = 40.0;
constexpr double MODERATE_CLAIM_THRESHOLD = 50.0;
constexpr double MANIPULATION_THRESHOLD = 25.0;
constexpr double SUSPICION_THRESHOLD = 60.0;

// Risk penalty amounts
constexpr double RISK_PENALTY_VERY_LOW_SOURCE = 8.0;
constexpr double RISK_PENALTY_LOW_SOURCE = 4.0;
constexpr double RISK_PENALTY_LOW_CLAIM_AND_SOURCE = 3.5;  // raised: weak source+claim combo is a real signal
constexpr double RISK_PENALTY_SUSPICIOUS_PATTERNS = 6.0;
constexpr double RISK_PENALTY_PER_UNCERTAINTY = 2.0;
constexpr double RISK_PENALTY_MAX_UNCERTAINTY = 7.0;
constexpr double RISK_PENALTY_CLAIM_MULTIPLIER = 0.40;     // raised: low claim verifiability should matter more
constexpr double RISK_PENALTY_COMBINED_LOW = 4.0;

// Consistency boost amounts — capped to prevent perfect 100 on short articles
constexpr double CONSISTENCY_BOOST_FACTUAL = 3.5;
constexpr double CONSISTENCY_BOOST_HIGH_CLAIM = 1.5;
constexpr double CONSISTENCY_BOOST_CLEAN_RECORD = 2.5;
constexpr double CONSISTENCY_BOOST_TRUSTED_SOURCE = 2.0;
constexpr double MAX_CONSISTENCY_BOOST = 7.0;              // hard cap on total boost

// Score combination weights — weights must sum to 1.0
constexpr double SOURCE_WEIGHT = 0.36;      // source DB is most reliable signal
constexpr double CLAIM_WEIGHT = 0.30;       // heuristic-only, keep influence moderate
constexpr double PREPROCESSING_WEIGHT = 0.17;
constexpr double DETECTION_WEIGHT = 0.17;

// Risk adjustment: detection avg above/below 75 nudges final score slightly
constexpr double RISK_ADJUSTMENT_CENTER = 75.0;
constexpr double RISK_ADJUSTMENT_MULTIPLIER = 0.15;  // reduced: detection shouldn't dominate

using newsscope::utils::clamp_score;
using newsscope::utils::to_lower_copy;
using newsscope::utils::count_phrase_hits;
using newsscope::utils::count_positive_phrase_hits;

struct ModuleScores {
    double preprocessing_score = 50.0;
    double source_score        = 50.0;
    double phrase_score        = 50.0;
    double kmp_score           = 50.0;
    double rabin_karp_score    = 50.0;
    double frequency_score     = 50.0;
    double temporal_score      = 50.0;
    double greedy_score        = 50.0;
    double claim_verifiability_score = 50.0;
};

// =============================================================================
// CONTEXT-AWARE PHRASE DETECTION
// =============================================================================

bool is_negation_context(const std::string& text, size_t pos) {
    if (pos < 4) return false;
    size_t start = (pos > 50) ? pos - 50 : 0;
    std::string context = text.substr(start, pos - start);
    
    static const std::vector<std::string> negation_contexts = {
        "debunk", "refut", "false claim", "disprove", "not true",
        "incorrect", "misleading claim", "fact check", "verify",
        "investigated", "found no evidence", "contrary to"
    };
    
    for (const auto& neg : negation_contexts) {
        if (context.find(neg) != std::string::npos) {
            return true;
        }
    }
    return false;
}

size_t count_context_aware_hits(const std::string& text, const std::vector<std::string>& patterns) {
    size_t hits = 0;
    std::string lower_text = to_lower_copy(text);
    
    for (const auto& pattern : patterns) {
        size_t pos = lower_text.find(pattern);
        while (pos != std::string::npos) {
            if (!is_negation_context(lower_text, pos)) {
                ++hits;
            }
            pos = lower_text.find(pattern, pos + 1);
        }
    }
    return hits;
}

std::string resolve_default_data_file(const std::string& filename) {
    const std::vector<std::string> candidates = {
        "data/" + filename,
        "../data/" + filename,
        "../../data/" + filename
    };
    for (const auto& path : candidates) {
        std::ifstream in(path);
        if (in.is_open()) {
            return path;
        }
    }
    return "";
}

const std::vector<std::string>& default_suspicious_phrases() {
    static const std::vector<std::string> phrases = {
        "fake news", "conspiracy theory", "you won't believe", "this one trick",
        "doctors hate", "click here", "act now", "shocking truth",
        "without evidence", "unverified claim", "anonymous source"
    };
    return phrases;
}

const std::vector<std::string>& suspicious_text_patterns() {
    static const std::vector<std::string> patterns = {
        "fake news", "hoax", "conspiracy", "exposed", "scandal",
        "unverified", "rumor", "without evidence", "anonymous source",
        "anonymous sources", "deep state", "cover-up", "suppressed report",
        "viral claim", "social media posts claim", "no official confirmation",
        "not yet been published", "leaked", "secret agenda", "do your own research",
        "hidden cure", "global elites", "secretly confirmed", "insiders revealed",
        "undeniable proof", "suppressed the report", "interdimensional beings",
        "covert operation", "global internet shutdown", "reportedly approved",
        "no official press release", "insiders have confirmed", "closed-door emergency meeting",
        "not yet been peer-reviewed", "technical reports are still pending",
        "unlimited energy", "without any environmental impact"
    };
    return patterns;
}

const std::vector<std::string>& factual_cues() {
    static const std::vector<std::string> cues = {
        "according to", "official report", "peer-reviewed", "peer reviewed",
        "data shows", "study found", "confirmed by", "documented", "audit", "evidence",
        "court ruled", "committee said", "ministry said", "agency said",
        "official statement", "statistics agency", "regulatory filing", "meeting minutes"
    };
    return cues;
}

const std::vector<std::string>& uncertainty_cues() {
    static const std::vector<std::string> cues = {
        "without evidence", "rumor", "allegedly", "unverified", "anonymous source",
        "social media posts claim", "not yet been published", "no official confirmation",
        "reportedly approved", "no official press release", "insiders have confirmed",
        "closed-door emergency meeting", "source we cannot name",
        "cannot be independently verified", "people in the know",
        "not yet been peer-reviewed", "technical reports are still pending",
        "reports are still pending"
    };
    return cues;
}

}  // namespace

namespace newsscope {

ScoringEngine::ScoringEngine()
    : preprocessor(std::make_unique<Preprocessor>()),
      source_validator(std::make_unique<SourceValidator>()),
      phrase_indexer(std::make_unique<PhraseIndexer>()),
      frequency_analyzer(std::make_unique<FrequencyAnalyzer>()),
      temporal_analyzer(std::make_unique<TemporalAnalyzer>()),
      greedy_filter(std::make_unique<GreedyFilter>()),
      claim_verifier(std::make_unique<ClaimVerifier>()) {}

void ScoringEngine::initialize(const std::string& sources_csv,
                              const std::string& suspicious_phrases_file,
                              const std::string& negative_terms_file) {
    std::lock_guard<std::mutex> lock(assess_mutex);
    if (initialized_resources &&
        sources_csv.empty() &&
        suspicious_phrases_file.empty() &&
        negative_terms_file.empty()) {
        return;
    }

    const std::string resolved_sources =
        sources_csv.empty() ? resolve_default_data_file("sources.csv") : sources_csv;
    const std::string resolved_phrases =
        suspicious_phrases_file.empty() ? resolve_default_data_file("suspicious_phrases.txt")
                                        : suspicious_phrases_file;
    const std::string resolved_negative_terms =
        negative_terms_file.empty() ? resolve_default_data_file("negative_terms.csv")
                                    : negative_terms_file;

    if (!resolved_sources.empty()) {
        source_validator->load_from_csv(resolved_sources);
    }

    bool loaded_phrases = false;
    if (!resolved_phrases.empty()) {
        loaded_phrases = phrase_indexer->load_from_file(resolved_phrases);
    }
    if (!loaded_phrases && phrase_indexer->phrase_count() == 0) {
        for (const auto& phrase : default_suspicious_phrases()) {
            phrase_indexer->insert_phrase(phrase);
        }
    }

    if (!resolved_negative_terms.empty()) {
        frequency_analyzer->load_negative_terms_from_file(resolved_negative_terms);
    }
    initialized_resources = true;
}

CredibilityResult ScoringEngine::assess_article(const Article& article) {
    CredibilityResult result;
    ModuleScores local_scores;
    std::vector<std::string> local_explanations;

    auto add_expl = [&](const std::string& module_name, double score, const std::string& reason) {
        std::stringstream ss;
        ss << "[" << module_name << "] Score: " << score << "/100 - " << reason;
        local_explanations.push_back(ss.str());
    };
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    const std::string combined_text = article.headline + " " + article.body;
    const std::string normalized_text = to_lower_copy(combined_text);

    // Pass combined_text directly to avoid re-concatenation inside process()
    auto tokens = preprocessor->tokenize(combined_text);
    preprocessor->remove_stop_words(tokens);

    const size_t factual_hits = count_positive_phrase_hits(normalized_text, factual_cues());
    const size_t uncertainty_hits = count_phrase_hits(normalized_text, uncertainty_cues());
    const double short_text_penalty = tokens.size() < SHORT_TEXT_THRESHOLD_CRITICAL ? SHORT_TEXT_PENALTY_CRITICAL 
                                    : (tokens.size() < SHORT_TEXT_THRESHOLD_WARNING ? SHORT_TEXT_PENALTY_WARNING : 0.0);

    local_scores.preprocessing_score = clamp_score(
        BASELINE_PREPROCESSING_SCORE 
        + static_cast<double>(factual_hits) * FACTUAL_CUE_BONUS
        - static_cast<double>(uncertainty_hits) * UNCERTAINTY_PENALTY
        - short_text_penalty
    );

    std::stringstream prep_msg;
    prep_msg << "Tokens: " << tokens.size()
             << ", factual cues: " << factual_hits
             << ", uncertainty cues: " << uncertainty_hits;
    add_expl("Preprocessing", local_scores.preprocessing_score, prep_msg.str());
    
    local_scores.source_score = source_validator->validate_source(article.source);
    std::stringstream source_msg;
    source_msg << "Source: " << article.source << " (credibility: "
               << local_scores.source_score << "/100)";
    add_expl("Source Validation", local_scores.source_score, source_msg.str());
    
    auto found_phrases = phrase_indexer->find_in_text(combined_text);
    double phrase_penalty = std::min(MAX_PHRASE_PENALTY, static_cast<double>(found_phrases.size()) * PHRASE_PENALTY_PER_HIT);
    local_scores.phrase_score = clamp_score(BASE_SCORE - phrase_penalty);
    std::stringstream phrase_msg;
    phrase_msg << "Found " << found_phrases.size() << " suspicious phrase(s)";
    add_expl("Phrase Indexing", local_scores.phrase_score, phrase_msg.str());
    
    const auto& malicious_patterns = suspicious_text_patterns();

    size_t kmp_matches = count_context_aware_hits(normalized_text, malicious_patterns);
    double kmp_penalty = std::min(MAX_KMP_PENALTY, static_cast<double>(kmp_matches) * KMP_PENALTY_PER_HIT);
    local_scores.kmp_score = clamp_score(BASE_SCORE - kmp_penalty);
    std::stringstream kmp_msg;
    kmp_msg << "KMP found " << kmp_matches << " suspicious pattern(s)";
    add_expl("KMP Matching", local_scores.kmp_score, kmp_msg.str());
    
    auto rk_results = StringMatcher::rabin_karp_multi_search(
        normalized_text,
        malicious_patterns
    );

    std::unordered_set<size_t> unique_pattern_hits;
    unique_pattern_hits.reserve(rk_results.size());
    for (const auto& entry : rk_results) {
        unique_pattern_hits.insert(entry.second);
    }
    double rk_penalty = std::min(MAX_RABIN_KARP_PENALTY, static_cast<double>(unique_pattern_hits.size()) * RABIN_KARP_PENALTY_PER_HIT);
    local_scores.rabin_karp_score = clamp_score(BASE_SCORE - rk_penalty);

    std::stringstream rk_msg;
    rk_msg << "Rabin-Karp found " << unique_pattern_hits.size() << " unique suspicious pattern(s)";
    add_expl("Rabin-Karp", local_scores.rabin_karp_score, rk_msg.str());
    
    frequency_analyzer->analyze(tokens, normalized_text);
    double freq_suspicion = frequency_analyzer->get_suspicion_score();
    double freq_penalty = std::min(MAX_FREQUENCY_PENALTY, freq_suspicion * FREQUENCY_PENALTY_MULTIPLIER);
    local_scores.frequency_score = clamp_score(BASE_SCORE - freq_penalty);
    auto top_negative = frequency_analyzer->get_top_negative_terms(3);
    std::stringstream freq_msg;
    freq_msg << "Found " << top_negative.size() << " negative term(s): ";
    for (const auto& entry : top_negative) {
        freq_msg << entry.term << " ";
    }
    add_expl("Frequency Analysis", local_scores.frequency_score, freq_msg.str());
    
    temporal_analyzer->add_entry(article.source, tokens.size(), article.timestamp);
    double temporal_spike = temporal_analyzer->get_spike_score();
    local_scores.temporal_score = clamp_score(BASE_SCORE - temporal_spike);
    std::stringstream temporal_msg;
    temporal_msg << "Temporal spike score: " << temporal_spike << "/100";
    add_expl("Temporal Analysis", local_scores.temporal_score, temporal_msg.str());
    
    double greedy_manipulation = greedy_filter->analyze_article(article.headline, article.body);
    local_scores.greedy_score = clamp_score(BASE_SCORE - greedy_manipulation);
    auto signals = greedy_filter->get_detected_signals();
    std::stringstream greedy_msg;
    greedy_msg << "Detected " << signals.size() << " manipulation signal(s)";
    add_expl("Greedy Filtering", local_scores.greedy_score, greedy_msg.str());

    const ClaimAssessment claim_assessment = claim_verifier->assess(article.headline, article.body);
    local_scores.claim_verifiability_score = claim_assessment.verifiability_score;
    std::stringstream claim_msg;
    claim_msg << "Evidence hits: " << claim_assessment.evidence_hits
              << ", attribution hits: " << claim_assessment.attribution_hits
              << ", uncertainty hits: " << claim_assessment.uncertainty_hits
              << ", promotional hits: " << claim_assessment.promotional_hits;
    add_expl("Claim Verifiability", local_scores.claim_verifiability_score, claim_msg.str());

    // Determine article structure quality
    const bool low_risk_structure =
        found_phrases.empty() &&
        kmp_matches == 0 &&
        unique_pattern_hits.empty() &&
        greedy_manipulation < 15.0 &&
        uncertainty_hits <= 1 &&
        freq_suspicion < SUSPICION_THRESHOLD;

    const bool high_quality_article =
        local_scores.source_score >= HIGH_SOURCE_THRESHOLD &&
        local_scores.claim_verifiability_score >= 60.0 &&
        factual_hits >= 1;

    // Risk penalty calculation - more graduated approach
    double risk_penalty = 0.0;
    
    // Source-based penalties (only for very low credibility sources)
    if (local_scores.source_score <= VERY_LOW_SOURCE_THRESHOLD) {
        risk_penalty += RISK_PENALTY_VERY_LOW_SOURCE;
    } else if (local_scores.source_score < LOW_SOURCE_CREDIBILITY_THRESHOLD) {
        risk_penalty += RISK_PENALTY_LOW_SOURCE;
    }
    
    // Combined source + claim weakness
    if (local_scores.source_score <= MEDIUM_SOURCE_CREDIBILITY_THRESHOLD &&
        local_scores.claim_verifiability_score < 58.0 &&
        !low_risk_structure) {
        risk_penalty += RISK_PENALTY_LOW_CLAIM_AND_SOURCE;
    }
    
    // Suspicious patterns detected — penalize regardless of source if signals are strong
    if ((kmp_matches >= 2 || greedy_manipulation > MANIPULATION_THRESHOLD || freq_suspicion > SUSPICION_THRESHOLD)) {
        const double pattern_penalty = local_scores.source_score >= HIGH_SOURCE_THRESHOLD
            ? RISK_PENALTY_SUSPICIOUS_PATTERNS * 0.6  // genuinely trusted sources get partial relief
            : RISK_PENALTY_SUSPICIOUS_PATTERNS;
        risk_penalty += pattern_penalty;
    }
    
    // Uncertainty without factual balance (only if severe)
    if (uncertainty_hits > 1 && factual_hits == 0 && !high_quality_article) {
        risk_penalty += std::min(RISK_PENALTY_MAX_UNCERTAINTY, 
                                 static_cast<double>(uncertainty_hits - 1) * RISK_PENALTY_PER_UNCERTAINTY);
    }
    
    // Very low claim verifiability (reduced threshold and multiplier)
    if (local_scores.claim_verifiability_score < LOW_CLAIM_VERIFIABILITY_THRESHOLD && !high_quality_article) {
        const double claim_penalty_multiplier = low_risk_structure ? 0.15 : RISK_PENALTY_CLAIM_MULTIPLIER;
        risk_penalty += (LOW_CLAIM_VERIFIABILITY_THRESHOLD - local_scores.claim_verifiability_score) * claim_penalty_multiplier;
    }
    
    // Combined weakness only for severe cases
    if (local_scores.source_score < LOW_SOURCE_CREDIBILITY_THRESHOLD && 
        local_scores.claim_verifiability_score < LOW_CLAIM_VERIFIABILITY_THRESHOLD) {
        risk_penalty += RISK_PENALTY_COMBINED_LOW;
    }

    // Consistency boosts
    double consistency_boost = 0.0;
    
    if (high_quality_article && low_risk_structure) {
        consistency_boost += CONSISTENCY_BOOST_FACTUAL;
    }
    if (local_scores.claim_verifiability_score >= 70.0) {
        consistency_boost += CONSISTENCY_BOOST_HIGH_CLAIM;
    }
    if (local_scores.source_score >= 50.0 &&
        local_scores.claim_verifiability_score >= 45.0 &&
        low_risk_structure) {
        consistency_boost += CONSISTENCY_BOOST_CLEAN_RECORD;
    }
    if (local_scores.source_score >= HIGH_SOURCE_THRESHOLD) {
        consistency_boost += CONSISTENCY_BOOST_TRUSTED_SOURCE;
    }
    // Hard cap: no article should reach 100 purely from boosts
    consistency_boost = std::min(consistency_boost, MAX_CONSISTENCY_BOOST);

    // Calculate detection module average
    const double detection_module_average =
        (local_scores.phrase_score +
         local_scores.kmp_score +
         local_scores.rabin_karp_score +
         local_scores.frequency_score +
         local_scores.temporal_score +
         local_scores.greedy_score) / 6.0;

    // REFINED scoring formula with proper weights
    const double credibility_core =
        (local_scores.source_score * SOURCE_WEIGHT) +
        (local_scores.claim_verifiability_score * CLAIM_WEIGHT) +
        (local_scores.preprocessing_score * PREPROCESSING_WEIGHT) +
        (detection_module_average * DETECTION_WEIGHT);
    
    const double risk_adjustment =
        (detection_module_average - RISK_ADJUSTMENT_CENTER) * RISK_ADJUSTMENT_MULTIPLIER;
    const double combined = credibility_core + risk_adjustment;

    result.overall_score = clamp_score(combined - risk_penalty + consistency_boost, 0.0, 97.0);
    result.module_scores = {
        {"preprocessing",      local_scores.preprocessing_score},
        {"source_validation",  local_scores.source_score},
        {"phrase_indexing",    local_scores.phrase_score},
        {"kmp_matching",       local_scores.kmp_score},
        {"rabin_karp",         local_scores.rabin_karp_score},
        {"frequency_analysis", local_scores.frequency_score},
        {"temporal_analysis",  local_scores.temporal_score},
        {"greedy_filtering",   local_scores.greedy_score},
        {"claim_verifiability",local_scores.claim_verifiability_score}
    };
    result.explanations = std::move(local_explanations);
    if (risk_penalty > 0.0 || consistency_boost > 0.0) {
        std::stringstream calibration_msg;
        calibration_msg << "Risk calibration applied (penalty: " << risk_penalty
                        << ", boost: " << consistency_boost << ")";
        result.explanations.push_back("[Score Calibration] " + calibration_msg.str());
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    result.processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time
    );
    
    return result;
}

std::vector<CredibilityResult> ScoringEngine::assess_batch(const std::vector<Article>& articles) {
    const size_t n = articles.size();
    std::vector<CredibilityResult> results(n);
    for (size_t i = 0; i < n; ++i) {
        results[i] = assess_article(articles[i]);
    }
    return results;
}

void ScoringEngine::set_module_weights(double preprocessing_weight,
                                     double source_weight,
                                     double phrase_weight,
                                     double kmp_weight,
                                     double rabin_karp_weight,
                                     double frequency_weight,
                                     double temporal_weight,
                                     double greedy_weight) {
    std::lock_guard<std::mutex> lock(assess_mutex);
    weights.preprocessing = preprocessing_weight;
    weights.source = source_weight;
    weights.phrase = phrase_weight;
    weights.kmp = kmp_weight;
    weights.rabin_karp = rabin_karp_weight;
    weights.frequency = frequency_weight;
    weights.temporal = temporal_weight;
    weights.greedy = greedy_weight;
}

// get_module_scores() and get_explanations() are superseded by CredibilityResult fields.
// Kept for API compatibility; callers should use assess_article() return value instead.
std::unordered_map<std::string, double> ScoringEngine::get_module_scores() const {
    return {};
}

std::vector<std::string> ScoringEngine::get_explanations() const {
    return {};
}

void ScoringEngine::reset() {
    std::lock_guard<std::mutex> lock(assess_mutex);
    initialized_resources = false;
    preprocessor    = std::make_unique<Preprocessor>();
    source_validator= std::make_unique<SourceValidator>();
    phrase_indexer  = std::make_unique<PhraseIndexer>();
    frequency_analyzer = std::make_unique<FrequencyAnalyzer>();
    temporal_analyzer  = std::make_unique<TemporalAnalyzer>();
    greedy_filter   = std::make_unique<GreedyFilter>();
    claim_verifier  = std::make_unique<ClaimVerifier>();
}

} // namespace newsscope
