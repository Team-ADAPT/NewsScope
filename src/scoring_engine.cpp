#include "scoring_engine.h"
#include "utils.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <unordered_set>
#include <cmath>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

namespace {

// =============================================================================
// SCORING CONSTANTS
// =============================================================================

// Base scores
constexpr double BASE_SCORE = 60.0;  // neutral baseline: "no evidence" ≠ "confirmed credible"
constexpr double DETECTION_MAX_SCORE = 100.0;  // detection modules can earn up to this via clean-text bonus
constexpr double BASELINE_PREPROCESSING_SCORE = 50.0;  // neutral baseline; no article should start inflated

// Text-length confidence: longer clean text earns more credibility on detection modules
constexpr size_t TEXT_LENGTH_FULL_CONFIDENCE = 80;  // token count for full confidence bonus
constexpr double CLEAN_TEXT_BONUS_MAX = 40.0;  // max bonus detection modules can earn (60+40=100)

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
constexpr double MANIPULATION_THRESHOLD = 25.0;
constexpr double SUSPICION_THRESHOLD = 60.0;

// Risk penalty amounts (Phase 4: increased penalties for better detection)
constexpr double RISK_PENALTY_VERY_LOW_SOURCE = 12.0;      // increased: 8.0 → 12.0
constexpr double RISK_PENALTY_LOW_SOURCE = 6.0;            // increased: 4.0 → 6.0
constexpr double RISK_PENALTY_LOW_CLAIM_AND_SOURCE = 5.5;  // increased: 3.5 → 5.5
constexpr double RISK_PENALTY_SUSPICIOUS_PATTERNS = 9.0;   // increased: 6.0 → 9.0
constexpr double RISK_PENALTY_PER_UNCERTAINTY = 3.0;       // increased: 2.0 → 3.0
constexpr double RISK_PENALTY_MAX_UNCERTAINTY = 10.0;      // increased: 7.0 → 10.0
constexpr double RISK_PENALTY_CLAIM_MULTIPLIER = 0.55;     // increased: 0.40 → 0.55 (weak claims matter more)
constexpr double RISK_PENALTY_COMBINED_LOW = 6.0;          // increased: 4.0 → 6.0

// Consistency boost amounts — Phase 3: increased boosts for quality articles
constexpr double CONSISTENCY_BOOST_FACTUAL = 5.0;           // increased: 3.5 → 5.0
constexpr double CONSISTENCY_BOOST_HIGH_CLAIM = 2.5;        // increased: 1.5 → 2.5
constexpr double CONSISTENCY_BOOST_CLEAN_RECORD = 4.0;      // increased: 2.5 → 4.0
constexpr double CONSISTENCY_BOOST_TRUSTED_SOURCE = 3.5;    // increased: 2.0 → 3.5
constexpr double CONSISTENCY_BOOST_GROUNDED_UNKNOWN_SOURCE = 3.5;  // increased: 2.5 → 3.5
constexpr double MAX_CONSISTENCY_BOOST = 12.0;              // increased: 7.0 → 12.0 (quality should be rewarded)

// Score combination weights — weights must sum to 1.0
constexpr double SOURCE_WEIGHT = 0.30;
constexpr double CLAIM_WEIGHT = 0.34;
constexpr double PREPROCESSING_WEIGHT = 0.12;  // reduced: preprocessing is less critical than detection
constexpr double DETECTION_WEIGHT = 0.24;      // increased: detection signals are more important

// Risk adjustment: detection avg above/below neutral nudges final score slightly
constexpr double RISK_ADJUSTMENT_CENTER = 60.0;
constexpr double RISK_ADJUSTMENT_MULTIPLIER = 0.12;  // reduced: detection shouldn't dominate

// ML fusion safety: avoid low-confidence ML outputs dragging strong deterministic assessments.
constexpr double ML_CONFIDENCE_GATE = 0.20;
constexpr double ML_CONFIDENCE_SCALER = 1.25;
constexpr double ML_LOW_CONFIDENCE_DOWNWEIGHT = 0.25;
constexpr double ML_HIGH_DETERMINISTIC_FLOOR = 75.0;
constexpr double ML_DOWNWARD_SHIFT_CAP = 6.0;

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

std::string resolve_project_file(const std::string& path) {
    const std::vector<std::string> candidates = {
        path,
        "./" + path,
        "../" + path,
        "../../" + path
    };
    for (const auto& candidate : candidates) {
        std::ifstream in(candidate);
        if (in.is_open()) {
            return candidate;
        }
    }
    return "";
}

bool env_flag_enabled(const char* name, bool default_value) {
    const char* raw = std::getenv(name);
    if (!raw) {
        return default_value;
    }
    const std::string value = to_lower_copy(raw);
    return !(value == "0" || value == "false" || value == "no" || value == "off");
}

double env_double_or_default(const char* name, double default_value) {
    const char* raw = std::getenv(name);
    if (!raw || *raw == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const double value = std::strtod(raw, &end);
    if (end == raw) {
        return default_value;
    }
    return value;
}

std::string trim_copy(const std::string& value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}


struct MlInferenceResult {
    bool ok = false;
    double probability = 0.0;
    std::string details;
    std::string error;
};

MlInferenceResult parse_ml_output(const std::string& output) {
    MlInferenceResult result;
    const std::string line = trim_copy(output);

    if (line.rfind("OK|", 0) == 0) {
        const size_t first = line.find('|', 3);
        if (first == std::string::npos) {
            result.error = "Malformed ML output";
            return result;
        }
        const std::string probability_str = line.substr(3, first - 3);
        const std::string details = line.substr(first + 1);
        char* end = nullptr;
        const double probability = std::strtod(probability_str.c_str(), &end);
        if (end == probability_str.c_str()) {
            result.error = "Invalid ML probability";
            return result;
        }
        result.ok = true;
        result.probability = probability;
        result.details = details.empty() ? "tokenizer" : details;
        return result;
    }

    if (line.rfind("ERR|", 0) == 0) {
        result.error = line.substr(4);
        return result;
    }

    result.error = line.empty() ? "No ML output" : line;
    return result;
}

MlInferenceResult run_ml_inference_ipc(const std::string& text, int write_fd, int read_fd, std::mutex& ipc_mutex) {
    std::lock_guard<std::mutex> lock(ipc_mutex);
    MlInferenceResult result;
    
    if (write_fd < 0 || read_fd < 0) {
        result.error = "ML IPC pipes not initialized";
        return result;
    }

    std::string escaped_text;
    for (char c : text) {
        if (c == '"') escaped_text += "\\\"";
        else if (c == '\\') escaped_text += "\\\\";
        else if (c == '\n') escaped_text += "\\n";
        else if (c == '\r') escaped_text += "\\r";
        else if (c == '\t') escaped_text += "\\t";
        else escaped_text += c;
    }
    std::string payload = "{\"text\": \"" + escaped_text + "\"}\n";
    
    if (write(write_fd, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) {
        result.error = "Failed to write to ML process";
        return result;
    }
    
    std::string response;
    char ch;
    while (read(read_fd, &ch, 1) == 1) {
        if (ch == '\n') break;
        response += ch;
    }
    
    if (response.empty()) {
        result.error = "Empty response from ML process";
        return result;
    }
    
    return parse_ml_output(response);
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

ScoringEngine::~ScoringEngine() {
    if (ml_child_pid > 0) {
        close(ml_write_fd);
        close(ml_read_fd);
        kill(ml_child_pid, SIGTERM);
        waitpid(ml_child_pid, nullptr, 0);
    }
}

void ScoringEngine::initialize(const std::string& sources_csv,
                              const std::string& suspicious_phrases_file,
                              const std::string& negative_terms_file) {
    std::lock_guard<std::mutex> lock(last_result_mutex);
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

    // ML inference is opt-in because the deterministic engine is the default
    // fast path and the ML overlay incurs Python process startup overhead.
    ml_enabled = env_flag_enabled("NEWSSCOPE_ENABLE_ML", false);
    ml_blend_weight = clamp_score(env_double_or_default("NEWSSCOPE_ML_BLEND_WEIGHT", 0.0), 0.0, 1.0);

    const char* model_path_env = std::getenv("NEWSSCOPE_ML_MODEL_PATH");
    const char* tokenizer_path_env = std::getenv("NEWSSCOPE_ML_TOKENIZER_PATH");
    const char* script_path_env = std::getenv("NEWSSCOPE_ML_SCRIPT_PATH");
    const char* articles_path_env = std::getenv("NEWSSCOPE_ML_ARTICLES_PATH");

    ml_model_path = (model_path_env && *model_path_env)
        ? std::string(model_path_env)
        : resolve_project_file("data/ml/tfidf_logreg.joblib");
    if (ml_model_path.empty()) {
        ml_model_path = "data/ml/tfidf_logreg.joblib";
    }
    ml_tokenizer_path = (tokenizer_path_env && *tokenizer_path_env)
        ? std::string(tokenizer_path_env)
        : "data/ml/tokenizer.json";
    ml_inference_script_path = (script_path_env && *script_path_env)
        ? std::string(script_path_env)
        : resolve_project_file("ml/model_inference.py");
    ml_articles_path = (articles_path_env && *articles_path_env)
        ? std::string(articles_path_env)
        : resolve_default_data_file("articles.json");

    if (ml_enabled) {
        std::ifstream model_stream(ml_model_path);
        std::ifstream script_stream(ml_inference_script_path);
        std::ifstream articles_stream(ml_articles_path);
        const bool has_serialized_model = model_stream.is_open();
        const bool can_train_from_articles = articles_stream.is_open();
        ml_enabled = script_stream.is_open() && (has_serialized_model || can_train_from_articles);
        
        if (ml_enabled && ml_child_pid < 0) {
            int pipe_to_child[2];
            int pipe_from_child[2];
            if (pipe(pipe_to_child) == 0 && pipe(pipe_from_child) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    dup2(pipe_to_child[0], STDIN_FILENO);
                    dup2(pipe_from_child[1], STDOUT_FILENO);
                    close(pipe_to_child[0]); close(pipe_to_child[1]);
                    close(pipe_from_child[0]); close(pipe_from_child[1]);
                    
                    std::vector<std::string> arg_storage = {
                        "python3", ml_inference_script_path,
                        "--model", ml_model_path,
                        "--tokenizer", ml_tokenizer_path,
                        "--server"
                    };
                    if (!ml_articles_path.empty()) {
                        arg_storage.push_back("--articles");
                        arg_storage.push_back(ml_articles_path);
                    }
                    std::vector<char*> argv;
                    for (auto& a : arg_storage) argv.push_back(a.data());
                    argv.push_back(nullptr);
                    
                    execvp("python3", argv.data());
                    _exit(127);
                } else if (pid > 0) {
                    close(pipe_to_child[0]);
                    close(pipe_from_child[1]);
                    ml_write_fd = pipe_to_child[1];
                    ml_read_fd = pipe_from_child[0];
                    ml_child_pid = pid;
                    
                    std::string ready_str;
                    char ch;
                    while (read(ml_read_fd, &ch, 1) == 1) {
                        if (ch == '\n') break;
                        ready_str += ch;
                    }
                    if (trim_copy(ready_str) != "READY") {
                        ml_enabled = false;
                        close(ml_write_fd);
                        close(ml_read_fd);
                        ml_write_fd = -1;
                        ml_read_fd = -1;
                    }
                } else {
                    ml_enabled = false;
                    close(pipe_to_child[0]);
                    close(pipe_to_child[1]);
                    close(pipe_from_child[0]);
                    close(pipe_from_child[1]);
                }
            } else {
                ml_enabled = false;
            }
        }
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
    
    // =========================================================================
    // RATIO-BASED MODULE SCORING
    // Each module computes: score = (passed_checks / total_checks) * confidence
    // confidence scales with text length — short text can never reach 100
    // =========================================================================
    const size_t token_count = tokens.size();
    const double text_confidence = std::min(1.0, static_cast<double>(token_count) / static_cast<double>(TEXT_LENGTH_FULL_CONFIDENCE));
    // Neutral midpoint: when we have no data, score is 50 (uncertain)
    // With full confidence and zero problems: score approaches 95
    // With full confidence and many problems: score approaches 0
    constexpr double NEUTRAL = 50.0;
    constexpr double MAX_CLEAN = 95.0;

    // --- Phrase Indexing: successful_clean / total_phrases_checked ---
    auto found_phrases = phrase_indexer->find_in_text(combined_text);
    const size_t total_phrase_patterns = default_suspicious_phrases().size();
    const size_t phrase_hits = found_phrases.size();
    const double phrase_clean_ratio = (total_phrase_patterns > 0)
        ? 1.0 - std::min(1.0, static_cast<double>(phrase_hits) / static_cast<double>(total_phrase_patterns))
        : 0.5;
    local_scores.phrase_score = clamp_score(
        NEUTRAL + (MAX_CLEAN - NEUTRAL) * phrase_clean_ratio * text_confidence
        - (1.0 - text_confidence) * (NEUTRAL - 30.0)  // low confidence pulls toward 30-50 range
    );
    std::stringstream phrase_msg;
    phrase_msg << "Found " << phrase_hits << "/" << total_phrase_patterns
              << " suspicious phrase(s), clean ratio: " << static_cast<int>(phrase_clean_ratio * 100) << "%";
    add_expl("Phrase Indexing", local_scores.phrase_score, phrase_msg.str());

    // --- KMP Matching: patterns_clean / total_patterns_checked ---
    const auto& malicious_patterns = suspicious_text_patterns();
    size_t kmp_matches = count_context_aware_hits(normalized_text, malicious_patterns);
    const size_t total_kmp_patterns = malicious_patterns.size();
    const double kmp_clean_ratio = (total_kmp_patterns > 0)
        ? 1.0 - std::min(1.0, static_cast<double>(kmp_matches) / static_cast<double>(total_kmp_patterns))
        : 0.5;
    local_scores.kmp_score = clamp_score(
        NEUTRAL + (MAX_CLEAN - NEUTRAL) * kmp_clean_ratio * text_confidence
        - (1.0 - text_confidence) * (NEUTRAL - 30.0)
    );
    std::stringstream kmp_msg;
    kmp_msg << "KMP matched " << kmp_matches << "/" << total_kmp_patterns
            << " patterns, clean ratio: " << static_cast<int>(kmp_clean_ratio * 100) << "%";
    add_expl("KMP Matching", local_scores.kmp_score, kmp_msg.str());

    // --- Rabin-Karp: unique_clean_patterns / total_patterns_checked ---
    auto rk_results = StringMatcher::rabin_karp_multi_search(
        normalized_text,
        malicious_patterns
    );
    std::unordered_set<size_t> unique_pattern_hits;
    unique_pattern_hits.reserve(rk_results.size());
    for (const auto& entry : rk_results) {
        unique_pattern_hits.insert(entry.second);
    }
    const double rk_clean_ratio = (total_kmp_patterns > 0)
        ? 1.0 - std::min(1.0, static_cast<double>(unique_pattern_hits.size()) / static_cast<double>(total_kmp_patterns))
        : 0.5;
    local_scores.rabin_karp_score = clamp_score(
        NEUTRAL + (MAX_CLEAN - NEUTRAL) * rk_clean_ratio * text_confidence
        - (1.0 - text_confidence) * (NEUTRAL - 30.0)
    );
    std::stringstream rk_msg;
    rk_msg << "Rabin-Karp matched " << unique_pattern_hits.size() << "/" << total_kmp_patterns
           << " unique patterns, clean ratio: " << static_cast<int>(rk_clean_ratio * 100) << "%";
    add_expl("Rabin-Karp", local_scores.rabin_karp_score, rk_msg.str());

    // --- Frequency Analysis: (100 - suspicion) / 100 ---
    auto freq_result = frequency_analyzer->analyze(tokens, normalized_text);
    double freq_suspicion = freq_result.suspicion_score;  // 0-100
    const double freq_clean_ratio = 1.0 - std::min(1.0, freq_suspicion / 100.0);
    local_scores.frequency_score = clamp_score(
        NEUTRAL + (MAX_CLEAN - NEUTRAL) * freq_clean_ratio * text_confidence
        - (1.0 - text_confidence) * (NEUTRAL - 30.0)
    );
    auto top_negative = freq_result.top_negative_terms;
    std::stringstream freq_msg;
    freq_msg << "Suspicion: " << static_cast<int>(freq_suspicion) << "/100, "
             << top_negative.size() << " negative term(s)";
    if (!top_negative.empty()) {
        freq_msg << ": ";
        for (const auto& entry : top_negative) {
            freq_msg << entry.term << " ";
        }
    }
    add_expl("Frequency Analysis", local_scores.frequency_score, freq_msg.str());

    // --- Temporal Analysis: (100 - spike_score) / 100 ---
    temporal_analyzer->add_entry(article.source, tokens.size(), article.timestamp);
    double temporal_spike = temporal_analyzer->get_spike_score();  // 0-100
    const double temporal_clean_ratio = 1.0 - std::min(1.0, temporal_spike / 100.0);
    local_scores.temporal_score = clamp_score(
        NEUTRAL + (MAX_CLEAN - NEUTRAL) * temporal_clean_ratio * text_confidence
        - (1.0 - text_confidence) * (NEUTRAL - 30.0)
    );
    std::stringstream temporal_msg;
    temporal_msg << "Spike score: " << temporal_spike << "/100"
                 << ", clean ratio: " << static_cast<int>(temporal_clean_ratio * 100) << "%";
    add_expl("Temporal Analysis", local_scores.temporal_score, temporal_msg.str());

    // --- Greedy Filtering: (100 - manipulation_score) / 100 ---
    auto greedy_result = greedy_filter->analyze_article(article.headline, article.body);
    double greedy_manipulation = greedy_result.manipulation_score;  // 0-100
    const double greedy_clean_ratio = 1.0 - std::min(1.0, greedy_manipulation / 100.0);
    local_scores.greedy_score = clamp_score(
        NEUTRAL + (MAX_CLEAN - NEUTRAL) * greedy_clean_ratio * text_confidence
        - (1.0 - text_confidence) * (NEUTRAL - 30.0)
    );
    auto signals = greedy_result.detected_signals;
    std::stringstream greedy_msg;
    greedy_msg << "Manipulation: " << static_cast<int>(greedy_manipulation) << "/100, "
               << signals.size() << " signal(s)"
               << ", clean ratio: " << static_cast<int>(greedy_clean_ratio * 100) << "%";
    add_expl("Greedy Filtering", local_scores.greedy_score, greedy_msg.str());

    // --- Claim Verifiability (uses its own scoring, not ratio-based) ---
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
    if (local_scores.source_score <= MEDIUM_SOURCE_CREDIBILITY_THRESHOLD &&
        local_scores.claim_verifiability_score >= 70.0 &&
        factual_hits >= 1 &&
        uncertainty_hits == 0 &&
        low_risk_structure) {
        consistency_boost += CONSISTENCY_BOOST_GROUNDED_UNKNOWN_SOURCE;
    }
    // Hard cap: no article should reach 100 purely from boosts
    consistency_boost = std::min(consistency_boost, MAX_CONSISTENCY_BOOST);

    // Calculate detection module average using dynamic weights
    const double detection_weight_sum =
        weights.phrase + weights.kmp + weights.rabin_karp +
        weights.frequency + weights.temporal + weights.greedy;
        
    const double detection_module_average = (detection_weight_sum > 0.0) ?
        ((local_scores.phrase_score * weights.phrase) +
         (local_scores.kmp_score * weights.kmp) +
         (local_scores.rabin_karp_score * weights.rabin_karp) +
         (local_scores.frequency_score * weights.frequency) +
         (local_scores.temporal_score * weights.temporal) +
         (local_scores.greedy_score * weights.greedy)) / detection_weight_sum : 50.0;

    // Calculate sum of all weights
    const double total_weight = 
        weights.preprocessing + weights.source + weights.claim_verifiability + detection_weight_sum;

    // REFINED scoring formula with dynamic weights
    const double credibility_core = (total_weight > 0.0) ?
        ((local_scores.source_score * weights.source) +
         (local_scores.claim_verifiability_score * weights.claim_verifiability) +
         (local_scores.preprocessing_score * weights.preprocessing) +
         (detection_module_average * detection_weight_sum)) / total_weight : 50.0;
    
    const double risk_adjustment =
        (detection_module_average - RISK_ADJUSTMENT_CENTER) * RISK_ADJUSTMENT_MULTIPLIER;
    const double combined = credibility_core + risk_adjustment;

    result.overall_score = clamp_score(combined - risk_penalty + consistency_boost, 0.0, 100.0);  // Phase 5: removed 97 ceiling
    result.deterministic_score = result.overall_score;
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
    {
        std::lock_guard<std::mutex> lock(last_result_mutex);
        last_module_scores = result.module_scores;
        last_explanations = result.explanations;
    }
    if (risk_penalty > 0.0 || consistency_boost > 0.0) {
        std::stringstream calibration_msg;
        calibration_msg << "Risk calibration applied (penalty: " << risk_penalty
                        << ", boost: " << consistency_boost << ")";
        result.explanations.push_back("[Score Calibration] " + calibration_msg.str());
        std::lock_guard<std::mutex> lock(last_result_mutex);
        last_explanations = result.explanations;
    }

    if (ml_enabled) {
        const MlInferenceResult ml = run_ml_inference_ipc(
            combined_text, ml_write_fd, ml_read_fd, ml_ipc_mutex
        );

        if (ml.ok) {
            // The ML script returns probability of credible/real class.
            const double ml_credibility_score = clamp_score(ml.probability * 100.0);
            result.ml_score = ml_credibility_score;

            std::stringstream ml_msg;
            ml_msg << std::fixed << std::setprecision(2)
                   << "Credible-class probability: " << (ml.probability * 100.0)
                   << "%, derived credibility: " << ml_credibility_score
                   << "/100 - " << ml.details;
            result.explanations.push_back("[ML Model] " + ml_msg.str());
            last_explanations = result.explanations;

            if (ml_blend_weight > 0.0) {
                const double deterministic_score = result.deterministic_score;
                const double ml_confidence = std::min(1.0, std::abs((ml.probability - 0.5) * 2.0));
                if (ml_confidence >= ML_CONFIDENCE_GATE) {
                    double effective_weight =
                        ml_blend_weight * std::min(1.0, ml_confidence * ML_CONFIDENCE_SCALER);
                    if (deterministic_score >= ML_HIGH_DETERMINISTIC_FLOOR &&
                        ml_credibility_score < deterministic_score &&
                        ml_confidence < 0.50) {
                        effective_weight *= ML_LOW_CONFIDENCE_DOWNWEIGHT;
                    }

                    const double blended_score =
                        ((1.0 - effective_weight) * deterministic_score) +
                        (effective_weight * ml_credibility_score);
                    const double protected_score = (deterministic_score >= ML_HIGH_DETERMINISTIC_FLOOR)
                        ? std::max(blended_score, deterministic_score - ML_DOWNWARD_SHIFT_CAP)
                        : blended_score;
                    result.overall_score = clamp_score(protected_score, 0.0, 100.0);  // Phase 5: removed 97 ceiling

                    std::stringstream fusion_msg;
                    fusion_msg << std::fixed << std::setprecision(2)
                               << "Applied blend weight " << effective_weight
                               << " (configured: " << ml_blend_weight
                               << ", confidence: " << (ml_confidence * 100.0) << "%"
                               << ", deterministic: " << deterministic_score
                               << ", ML-derived: " << ml_credibility_score << ")";
                    result.explanations.push_back("[ML Fusion] " + fusion_msg.str());
                    last_explanations = result.explanations;
                } else {
                    std::stringstream fusion_msg;
                    fusion_msg << std::fixed << std::setprecision(2)
                               << "Skipped ML blend due to low confidence ("
                               << (ml_confidence * 100.0)
                               << "%; gate: " << (ML_CONFIDENCE_GATE * 100.0) << "%)";
                    result.explanations.push_back("[ML Fusion] " + fusion_msg.str());
                    last_explanations = result.explanations;
                }
            }
        } else {
            result.explanations.push_back("[ML Model] Inference unavailable: " + ml.error);
            last_explanations = result.explanations;
        }
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
                                     double greedy_weight,
                                     double claim_weight) {
    std::lock_guard<std::mutex> lock(last_result_mutex);
    weights.preprocessing = preprocessing_weight;
    weights.source = source_weight;
    weights.phrase = phrase_weight;
    weights.kmp = kmp_weight;
    weights.rabin_karp = rabin_karp_weight;
    weights.frequency = frequency_weight;
    weights.temporal = temporal_weight;
    weights.greedy = greedy_weight;
    weights.claim_verifiability = claim_weight;
}

// get_module_scores() and get_explanations() are superseded by CredibilityResult fields.
// Kept for API compatibility; callers should use assess_article() return value instead.
std::unordered_map<std::string, double> ScoringEngine::get_module_scores() const {
    std::lock_guard<std::mutex> lock(last_result_mutex);
    return last_module_scores;
}

std::vector<std::string> ScoringEngine::get_explanations() const {
    std::lock_guard<std::mutex> lock(last_result_mutex);
    return last_explanations;
}

void ScoringEngine::reset() {
    std::lock_guard<std::mutex> lock(last_result_mutex);
    initialized_resources = false;
    ml_enabled = false;
    ml_blend_weight = 0.0;
    ml_model_path.clear();
    ml_tokenizer_path.clear();
    ml_inference_script_path.clear();
    ml_articles_path.clear();
    last_module_scores.clear();
    last_explanations.clear();
    preprocessor    = std::make_unique<Preprocessor>();
    source_validator= std::make_unique<SourceValidator>();
    phrase_indexer  = std::make_unique<PhraseIndexer>();
    frequency_analyzer = std::make_unique<FrequencyAnalyzer>();
    temporal_analyzer  = std::make_unique<TemporalAnalyzer>();
    greedy_filter   = std::make_unique<GreedyFilter>();
    claim_verifier  = std::make_unique<ClaimVerifier>();
}

} // namespace newsscope
