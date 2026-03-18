#include "claim_verifier.h"

#include <cassert>
#include <iostream>

using namespace newsscope;

void test_basic_assessment_range() {
    std::cout << "Testing Claim Verifier Score Range...\n";
    ClaimVerifier verifier;
    auto assessment = verifier.assess(
        "City budget hearing completed",
        "Officials said the budget draft will be published next week."
    );
    assert(assessment.verifiability_score >= 0.0 && assessment.verifiability_score <= 100.0);
    std::cout << "  ✓ Score in range: " << assessment.verifiability_score << "\n";
}

void test_promotional_text_penalty() {
    std::cout << "Testing Promotional Claim Penalty...\n";
    ClaimVerifier verifier;

    auto grounded = verifier.assess(
        "Audit summary released",
        "According to the official report, independent auditors documented the variance."
    );

    auto promotional = verifier.assess(
        "Futuristic leap announced",
        "The initiative promises to create millions of jobs and position the nation as world leader by 2035."
    );

    assert(grounded.verifiability_score > promotional.verifiability_score);
    std::cout << "  ✓ Grounded score: " << grounded.verifiability_score
              << ", promotional score: " << promotional.verifiability_score << "\n";
}

void test_real_vs_templated_narrative() {
    std::cout << "Testing Real vs Templated Narrative Differentiation...\n";
    ClaimVerifier verifier;

    auto real_like = verifier.assess(
        "Energy route risks increase",
        "Iran's attacks on Gulf nations and its grip on the Strait of Hormuz have sparked concerns of a global energy crisis."
    );

    auto templated_fake_like = verifier.assess(
        "Futuristic initiative announced",
        "In a surprising turn of events, the plan dubbed Digital Bharat 2035 promises to create millions of jobs and position India as a world leader in innovation. Supporters have praised the vision as bold and forward-thinking, while critics have raised concerns."
    );

    assert(real_like.verifiability_score > templated_fake_like.verifiability_score);
    std::cout << "  ✓ Real-like score: " << real_like.verifiability_score
              << ", templated score: " << templated_fake_like.verifiability_score << "\n";
}

void test_unverified_global_shutdown_claim_penalty() {
    std::cout << "Testing Unverified Global Shutdown Claim Penalty...\n";
    ClaimVerifier verifier;

    auto assessment = verifier.assess(
        "Global internet shutdown planned",
        "The United Nations has reportedly approved a coordinated global internet shutdown scheduled for next week. "
        "While no official press release has been published, several insiders have confirmed the decision was made "
        "during a closed-door emergency meeting."
    );

    assert(assessment.verifiability_score < 45.0);
    std::cout << "  ✓ Unverified shutdown claim score: " << assessment.verifiability_score << "\n";
}

void test_negated_peer_review_does_not_count_as_evidence() {
    std::cout << "Testing Negated Peer Review Handling...\n";
    ClaimVerifier verifier;

    auto assessment = verifier.assess(
        "Researchers claim unlimited-energy breakthrough",
        "The findings have not yet been peer-reviewed, detailed technical reports are still pending, "
        "and the device is claimed to produce unlimited energy without any environmental impact."
    );

    assert(assessment.evidence_hits == 0);
    assert(assessment.uncertainty_hits >= 2);
    assert(assessment.verifiability_score < 50.0);
    std::cout << "  ✓ Negated evidence no longer inflates credibility: "
              << assessment.verifiability_score << "\n";
}

void test_contractions_do_not_count_as_quotes() {
    std::cout << "Testing Contraction Quote Handling...\n";
    ClaimVerifier verifier;

    auto assessment = verifier.assess(
        "You Won't Believe What This Simple Trick Does to Your Phone Battery Life!",
        ""
    );

    assert(!assessment.has_quotes);
    assert(assessment.verifiability_score < 45.0);
    std::cout << "  ✓ Apostrophes in contractions no longer boost credibility\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "CLAIM VERIFIER TESTS\n";
    std::cout << std::string(60, '=') << "\n\n";

    try {
        test_basic_assessment_range();
        test_promotional_text_penalty();
        test_real_vs_templated_narrative();
        test_unverified_global_shutdown_claim_penalty();
        test_negated_peer_review_does_not_count_as_evidence();
        test_contractions_do_not_count_as_quotes();

        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "All claim verifier tests passed! ✓\n";
        std::cout << std::string(60, '=') << "\n\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
