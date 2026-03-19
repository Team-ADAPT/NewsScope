#include "claim_verifier.h"
#include "utils.h"

#include <cctype>
#include <vector>

namespace {

using newsscope::utils::clamp_score;
using newsscope::utils::to_lower_copy;
using newsscope::utils::count_phrase_hits;
using newsscope::utils::count_positive_phrase_hits;

int count_numeric_claims(const std::string& text) {
    int numeric_hits = 0;
    bool in_number = false;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (!in_number) {
                ++numeric_hits;
                in_number = true;
            }
        } else {
            in_number = false;
        }
    }
    return numeric_hits;
}

size_t word_count(const std::string& text) {
    size_t words = 0;
    bool in_word = false;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            if (!in_word) {
                ++words;
                in_word = true;
            }
        } else {
            in_word = false;
        }
    }
    return words;
}

bool is_quote_like_single_quote(const std::string& text, size_t pos) {
    const bool left_alpha =
        pos > 0 && std::isalpha(static_cast<unsigned char>(text[pos - 1]));
    const bool right_alpha =
        pos + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[pos + 1]));
    return !(left_alpha && right_alpha);
}

bool contains_quoted_text(const std::string& headline, const std::string& body) {
    if (headline.find('"') != std::string::npos || body.find('"') != std::string::npos) {
        return true;
    }

    const std::string combined = headline + " " + body;
    for (size_t i = 0; i < combined.size(); ++i) {
        if (combined[i] == '\'' && is_quote_like_single_quote(combined, i)) {
            return true;
        }
    }
    return false;
}

bool has_year_reference(const std::string& text) {
    for (size_t i = 0; i + 3 < text.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(text[i])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 2])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 3]))) {
            const int year = (text[i] - '0') * 1000 +
                             (text[i + 1] - '0') * 100 +
                             (text[i + 2] - '0') * 10 +
                             (text[i + 3] - '0');
            if (year >= 2027 && year <= 2100) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

namespace newsscope {

ClaimAssessment ClaimVerifier::assess(const std::string& headline, const std::string& body) const {
    const std::string text = to_lower_copy(headline + " " + body);
    ClaimAssessment result;

    static const std::vector<std::string> evidence_markers = {
        "according to", "official report", "official statement", "ministry said",
        "agency said", "court filing", "court ruled", "committee said",
        "regulator said", "documented", "audit", "data shows",
        "study found", "survey found", "peer-reviewed", "published in",
        "regulatory filing", "meeting minutes", "senator said", "president said",
        "spokesman said", "spokeswoman said", "posted on", "tweeted", "told reporters",
        "in a statement", "press conference", "news conference", "briefing"
    };

    static const std::vector<std::string> attribution_markers = {
        "according to", "officials said", "authorities said", "spokesperson said",
        "minister said", "agency said", "ministry said", "court filing",
        "court said", "committee said", "regulator said", "police said",
        "researchers said", "the report said", "the study said",
        "announced in a statement", "issued a statement", "published a report",
        "senator said", "congressman said", "lawmaker said", "diplomat said",
        "foreign minister", "prime minister", "president said", "white house said",
        "pentagon said", "state department", "told reporters", "in an interview",
        "said on tuesday", "said on monday", "said on wednesday", "said on thursday",
        "said on friday", "said on saturday", "said on sunday",
        "one of his closest allies said", "allies said", "sources said"
    };

    static const std::vector<std::string> grounding_markers = {
        "court", "ministry", "agency", "committee", "central bank", "parliament",
        "government", "police", "journal", "study", "survey", "audit",
        "statistics", "report", "filing", "statement", "investigation",
        "research team", "researchers", "review board", "regulator",
        "senator", "congressman", "lawmaker", "diplomat", "official",
        "president", "prime minister", "foreign minister", "state department",
        "pentagon", "white house", "european", "nato", "united nations"
    };

    static const std::vector<std::string> uncertainty_markers = {
        "unverified", "rumor", "allegedly", "sources say", "it is believed",
        "not yet been published", "no official confirmation", "social media posts claim",
        "secretly", "hidden cure", "global elites", "insiders revealed", "undeniable proof",
        "interdimensional beings", "covert operation", "reportedly approved",
        "no official press release", "no official press release has been published",
        "insiders have confirmed", "several insiders have confirmed",
        "closed-door emergency meeting", "closed door emergency meeting",
        "not yet been peer-reviewed", "has not yet been peer-reviewed",
        "technical reports are still pending", "detailed technical reports are still pending",
        "reports are still pending", "still pending",
        "claim to have discovered", "being hailed as",
        "insider", "whistleblower", "classified document", "leaked document",
        "surfaces online", "share before deletion", "quietly signed",
        "finally revealed", "mainstream refuses to report", "they are hiding this"
    };

    static const std::vector<std::string> sensational_markers = {
        "shocking", "unbelievable", "explosive", "bombshell", "secret",
        "you won't believe", "jaw-dropping", "breaking!!!", "secretly confirmed",
        "interdimensional", "miracle", "stunned", "baffled"
    };

    static const std::vector<std::string> promotional_markers = {
        "defining moment", "world leader", "futuristic", "bold and forward-thinking",
        "transforming", "millions of jobs", "revolutionary", "historic initiative",
        "promises to create", "positioning as", "breakthrough is being hailed",
        "revolutionary step", "solving global energy challenges"
    };

    static const std::vector<std::string> templated_narrative_markers = {
        "in a surprising turn of events",
        "has sparked widespread debate",
        "supporters have praised",
        "critics have raised concerns",
        "a defining moment for",
        "aimed at transforming",
        "dubbed"
    };

    static const std::vector<std::string> extraordinary_claim_markers = {
        "unlimited energy", "without any environmental impact",
        "solving global energy challenges", "miracle cure", "changes everything"
    };

    static const std::vector<std::string> conspiratorial_markers = {
        "deep state", "cover-up", "cover up", "global bankers", "globalist cabal",
        "cabal", "crisis actors", "chemtrails", "plandemic", "population control",
        "mind control", "false flag", "secret tribunal", "bank account freeze",
        "all cancers", "all disease", "overnight", "shadow government"
    };

    result.evidence_hits = count_positive_phrase_hits(text, evidence_markers);
    result.attribution_hits = count_positive_phrase_hits(text, attribution_markers);
    result.uncertainty_hits = count_phrase_hits(text, uncertainty_markers);
    result.sensational_hits = count_phrase_hits(text, sensational_markers);
    result.promotional_hits = count_phrase_hits(text, promotional_markers);
    const int grounding_hits = count_phrase_hits(text, grounding_markers);
    const int template_hits = count_phrase_hits(text, templated_narrative_markers);
    const int extraordinary_hits = count_phrase_hits(text, extraordinary_claim_markers);
    const int conspiratorial_hits = count_phrase_hits(text, conspiratorial_markers);
    const bool no_official_release =
        text.find("no official press release") != std::string::npos ||
        text.find("no official statement") != std::string::npos;
    const bool insider_only_sourcing =
        text.find("insiders have confirmed") != std::string::npos ||
        text.find("several insiders have confirmed") != std::string::npos;
    const bool closed_door_claim =
        text.find("closed-door emergency meeting") != std::string::npos ||
        text.find("closed door emergency meeting") != std::string::npos;
    result.numeric_claims = count_numeric_claims(text);
    result.has_quotes = contains_quoted_text(headline, body);
    const bool weak_support =
        result.evidence_hits == 0 && result.attribution_hits == 0 && grounding_hits < 2;
    const bool has_grounded_specificity =
        grounding_hits >= 2 &&
        (result.evidence_hits > 0 || result.attribution_hits > 0 || result.numeric_claims > 0);

    // Improved baseline - 50 gives neutral starting point
    double score = 50.0;
    
    // Positive signals (increased weights for attribution)
    score += std::min(25.0, static_cast<double>(result.evidence_hits) * 6.0);
    score += std::min(15.0, static_cast<double>(result.attribution_hits) * 4.0);
    score += std::min(12.0, static_cast<double>(grounding_hits) * 2.5);
    if (!weak_support) {
        score += std::min(6.0, static_cast<double>(result.numeric_claims) * 1.5);
    }
    if (result.has_quotes) {
        score += 4.0;
    }
    if (has_grounded_specificity) {
        score += 5.0;
    }

    // Negative signals (reduced aggressiveness)
    score -= std::min(28.0, static_cast<double>(result.uncertainty_hits) * 5.5);
    score -= std::min(20.0, static_cast<double>(result.sensational_hits) * 5.0);
    score -= std::min(24.0, static_cast<double>(result.promotional_hits) * 6.0);
    score -= std::min(28.0, static_cast<double>(conspiratorial_hits) * 5.5);

    const bool unsupported_specific_claims =
        result.numeric_claims > 0 && weak_support;
    if (unsupported_specific_claims) {
        score -= 8.0;
        result.flags.push_back("Numeric claims without evidence/attribution");
    }

    if (has_year_reference(text) && result.evidence_hits == 0) {
        score -= 6.0;
        result.flags.push_back("Future-year claim without explicit evidence");
    }

    if (template_hits >= 4 && result.evidence_hits == 0) {
        score -= 14.0;
        result.flags.push_back("Templated narrative style with weak sourcing");
    }

    if (has_year_reference(text) && result.promotional_hits > 0 && result.evidence_hits == 0) {
        score -= 8.0;
        result.flags.push_back("Future-looking promotional claim lacks evidence");
    }

    if (word_count(text) > 100 && weak_support) {
        score -= 8.0;
        result.flags.push_back("Long narrative with no concrete sourcing markers");
    }

    if (no_official_release) {
        score -= 10.0;
        result.flags.push_back("Claim references lack of official press release");
    }

    if (insider_only_sourcing && result.evidence_hits == 0) {
        score -= 8.0;
        result.flags.push_back("Insider-only sourcing without verifiable evidence");
    }

    if (closed_door_claim && result.evidence_hits == 0) {
        score -= 6.0;
        result.flags.push_back("Closed-door emergency claim without primary evidence");
    }

    if (conspiratorial_hits > 0) {
        result.flags.push_back("Conspiratorial framing reduces verifiability");
    }

    if (result.promotional_hits > 0 && result.evidence_hits == 0) {
        result.flags.push_back("Promotional framing with weak verifiability");
    }

    if (weak_support && result.uncertainty_hits > 0) {
        score -= 8.0;
        result.flags.push_back("Uncertain claim lacks concrete sourcing");
    }

    if (result.evidence_hits == 0 && result.promotional_hits > 0 && result.uncertainty_hits > 0) {
        score -= 6.0;
        result.flags.push_back("Promotional uncertain framing");
    }

    if (extraordinary_hits > 0 && (result.evidence_hits == 0 || result.uncertainty_hits > 0)) {
        score -= std::min(18.0, static_cast<double>(extraordinary_hits) * 9.0);
        result.flags.push_back("Extraordinary claim lacks strong verification");
    }

    result.verifiability_score = clamp_score(score);
    return result;
}

}  // namespace newsscope
