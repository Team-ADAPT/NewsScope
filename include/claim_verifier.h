#ifndef NEWSSCOPE_CLAIM_VERIFIER_H
#define NEWSSCOPE_CLAIM_VERIFIER_H

#include <string>
#include <vector>

namespace newsscope {

struct ClaimAssessment {
    double verifiability_score = 50.0;
    int evidence_hits = 0;
    int attribution_hits = 0;
    int uncertainty_hits = 0;
    int sensational_hits = 0;
    int promotional_hits = 0;
    int numeric_claims = 0;
    bool has_quotes = false;
    std::vector<std::string> flags;
};

class ClaimVerifier {
public:
    ClaimAssessment assess(const std::string& headline, const std::string& body) const;
};

}

#endif
