#include "xai/decision.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace xai::decision;
using xai::contracts::Exactness;
using xai::contracts::Status;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] Rational fraction(unsigned long numerator, unsigned long denominator) {
    Rational value(numerator, denominator);
    value.canonicalize();
    return value;
}

constexpr const char* kSubject = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* kClaim = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

[[nodiscard]] xai::contracts::CanonicalValue exact_wmc_value() {
    xai::contracts::CanonicalValue::Object object;
    object.emplace("$rational", xai::contracts::CanonicalValue::Array{
        xai::contracts::CanonicalValue{"3"}, xai::contracts::CanonicalValue{"7"}});
    return object;
}

class TestVerifier final : public CertificateVerifier {
public:
    bool accept{true};
    bool throw_error{false};
    mutable unsigned int calls{0};
    [[nodiscard]] std::string_view id() const noexcept override { return "test.wmc.verifier"; }
    [[nodiscard]] std::string_view version() const noexcept override { return "1"; }
    [[nodiscard]] VerificationResult verify(
        const Certificate& certificate, std::string_view expected_property_id,
        std::string_view expected_subject_sha256,
        std::string_view expected_claim_sha256,
        const xai::contracts::CanonicalValue& output_value) const override {
        ++calls;
        if (throw_error) throw std::runtime_error("fixture verifier exception");
        const bool expected_value = output_value == exact_wmc_value();
        const bool binding_matches = certificate.property_id == expected_property_id &&
            certificate.subject_sha256 == expected_subject_sha256 &&
            certificate.claim_sha256 == expected_claim_sha256 && expected_value;
        return {accept && binding_matches,
                accept && binding_matches ? std::string(expected_property_id) : "",
                "synthetic test verifier result"};
    }
};

[[nodiscard]] Certificate make_certificate() {
    return {xai::contracts::CertificateId{"certificate:phase7-test"},
            "wmc.emit_exact_result", kSubject, kClaim,
            std::string(kWmcExactResultProperty), "test.wmc.verifier", "1", 10, 100};
}

[[nodiscard]] CandidateAction certified_action(std::string id = "wmc.emit_exact_result") {
    const std::vector<StateProbability> states{{"exact", fraction(3, 4), false},
                                               {"wrong_or_incomplete", fraction(1, 4), false}};
    CandidateAction action = make_wmc_exact_result_action(
        states, fraction(1, 8), exact_wmc_value(),
        kClaim, make_certificate());
    action.action_id = std::move(id);
    action.certificate->action_id = action.action_id;
    action.certificate->certificate_id =
        xai::contracts::CertificateId{"certificate:" + action.action_id};
    return action;
}

[[nodiscard]] DecisionRequest base_request() {
    DecisionRequest request;
    request.subject_sha256 = kSubject;
    request.states = {{"exact", fraction(3, 4), false},
                      {"wrong_or_incomplete", fraction(1, 4), false}};
    request.actions = {certified_action()};
    request.lineage = {"query:wmc-17", "model:exact-solver-v1"};
    request.assumptions = {"Losses and state masses are supplied by the declared task policy."};
    request.limitations = {"Fixture-only; no corpus outcomes or empirical risk estimates."};
    return request;
}

[[nodiscard]] DecisionPolicy base_policy() {
    DecisionPolicy policy;
    policy.resource_cost_weight = Rational{1};
    policy.abstention_cost = Rational{1};
    policy.maximum_unresolved_probability = Rational{0};
    policy.verifier_id = "test.wmc.verifier";
    policy.verifier_version = "1";
    policy.decision_time_epoch = 50;
    return policy;
}

void test_exact_risk_ordering_and_certificate_gate() {
    auto request = base_request();
    CandidateAction lower_loss;
    lower_loss.action_id = "wmc.emit_backup";
    lower_loss.label = "Backup candidate";
    lower_loss.output_value = xai::contracts::CanonicalValue{"backup-result"};
    lower_loss.claim_sha256 = std::string(64, 'c');
    lower_loss.loss_by_state = {{"exact", fraction(1, 2)},
                                {"wrong_or_incomplete", fraction(1, 2)}};
    lower_loss.resource_cost = fraction(1, 20);
    request.actions.push_back(lower_loss);

    TestVerifier verifier;
    const auto output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::selected && output.action_id ==
                std::optional<std::string>("wmc.emit_exact_result"),
            "lowest exact-rational expected risk should be selected");
    require(output.expected_risk == std::optional<Rational>(fraction(3, 8)),
            "expected risk must equal 1/4 loss plus 1/8 resource cost");
    require(output.resource_cost == std::optional<Rational>(fraction(1, 8)),
            "selected resource cost must be reported exactly");
    require(output.assessments.size() == 2 &&
                output.assessments[1].certificate_status == CertificateStatus::verified,
            "required certificate must be independently accepted before ranking");
}

void test_risk_ties_and_abstention_tie_precedence() {
    auto request = base_request();
    auto action = certified_action();
    action.loss_by_state = {{"exact", fraction(7, 8)},
                            {"wrong_or_incomplete", fraction(7, 8)}};
    action.resource_cost = fraction(1, 8);
    request.actions = {action};
    TestVerifier verifier;
    auto output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.expected_risk == std::optional<Rational>(Rational{1}),
            "abstention must win an exact tie with action risk");

    action.loss_by_state = {{"exact", Rational{0}},
                            {"wrong_or_incomplete", Rational{0}}};
    action.action_id = "wmc.z_action";
    action.certificate->action_id = action.action_id;
    action.certificate->certificate_id = xai::contracts::CertificateId{"certificate:wmc.z_action"};
    auto second = action;
    second.action_id = "wmc.a_action";
    second.certificate->action_id = second.action_id;
    second.certificate->certificate_id = xai::contracts::CertificateId{"certificate:wmc.a_action"};
    request.actions = {action, second};
    output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::selected &&
                output.action_id == std::optional<std::string>("wmc.a_action"),
            "equal-risk actions must use deterministic lexical tie-breaking");
    action.tie_break_priority = -1;
    second.tie_break_priority = 0;
    request.actions = {action, second};
    output = decide(request, base_policy(), &verifier);
    require(output.action_id == std::optional<std::string>("wmc.z_action"),
            "explicit priority must precede lexical fallback");
}

void test_invalid_probability_and_unavailable_loss() {
    auto request = base_request();
    request.states[0].probability = fraction(2, 3);
    const auto invalid_mass = decide(request, base_policy());
    require(invalid_mass.status == DecisionStatus::invalid_input &&
                !invalid_mass.action_id && !invalid_mass.expected_risk,
            "non-unit probability mass must fail explicitly without fabricated risk");

    request = base_request();
    request.actions[0].loss_by_state.erase("wrong_or_incomplete");
    const auto missing_loss = decide(request, base_policy());
    require(missing_loss.status == DecisionStatus::invalid_input &&
                missing_loss.diagnostic.find("exactly one loss") != std::string::npos,
            "unavailable state loss must fail closed");

    request = base_request();
    auto duplicate = request.actions.front();
    duplicate.action_id = "wmc.duplicate";
    duplicate.certificate->action_id = duplicate.action_id;
    request.actions.push_back(duplicate);
    const auto duplicate_certificate = decide(request, base_policy());
    require(duplicate_certificate.status == DecisionStatus::invalid_input &&
                duplicate_certificate.diagnostic.find("certificate IDs") != std::string::npos,
            "duplicate certificate IDs in one request must be rejected");

    request = base_request();
    request.states.clear();
    for (std::size_t index = 0; index < kMaximumDecisionStates + 1; ++index) {
        request.states.push_back({"state." + std::to_string(index),
                                  index == 0 ? Rational{1} : Rational{0}, false});
    }
    const auto oversized = decide(request, base_policy());
    require(oversized.status == DecisionStatus::invalid_input &&
                oversized.diagnostic.find("256-state") != std::string::npos,
            "more than the declared state cap must fail explicitly");
}

void test_missing_stale_and_mismatched_certificates() {
    TestVerifier verifier;
    auto request = base_request();
    request.actions[0].certificate.reset();
    auto output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.assessments[0].certificate_status == CertificateStatus::missing,
            "missing required certificate must exclude the action");

    request = base_request();
    request.actions[0].certificate->expires_at_epoch = 49;
    output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.assessments[0].certificate_status == CertificateStatus::stale,
            "expired certificate must exclude the action");

    request = base_request();
    request.actions[0].certificate->subject_sha256 = std::string(64, 'c');
    output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.assessments[0].certificate_status == CertificateStatus::mismatch,
            "certificate bound to another input must be rejected");

    request = base_request();
    request.actions[0].certificate->claim_sha256 = "bad";
    output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.assessments[0].certificate_status == CertificateStatus::invalid,
            "malformed certificate must be rejected distinctly");
}

void test_verifier_failure_and_exception() {
    auto request = base_request();
    TestVerifier verifier;
    request.actions[0].output_value = xai::contracts::CanonicalValue{"different-result"};
    auto output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.assessments[0].certificate_status == CertificateStatus::verifier_rejected,
            "verifier must evaluate the actual candidate value, not only certificate metadata");

    request = base_request();
    verifier.accept = false;
    output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.assessments[0].certificate_status == CertificateStatus::verifier_rejected,
            "verifier rejection must exclude the action");

    verifier.accept = true;
    verifier.throw_error = true;
    output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.assessments[0].certificate_status == CertificateStatus::verifier_rejected,
            "verifier exception must fail closed");
}

void test_no_certified_action_resource_and_unresolved_abstention() {
    auto request = base_request();
    request.actions[0].certificate.reset();
    auto output = decide(request, base_policy());
    require(output.status == DecisionStatus::abstention && !output.action_id,
            "no certified action must leave abstention available");

    request = base_request();
    request.actions[0].resource_cost = Rational{2};
    TestVerifier verifier;
    output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.assessments[0].certificate_status == CertificateStatus::resource_limit &&
                verifier.calls == 0,
            "over-budget actions must be inadmissible");

    request = base_request();
    request.states[1].unresolved = true;
    output = decide(request, base_policy(), &verifier);
    require(output.status == DecisionStatus::abstention &&
                output.unresolved_probability == std::optional<Rational>(fraction(1, 4)) &&
                output.assessments[0].certificate_status == CertificateStatus::not_evaluated,
            "unresolved probability above the zero default must force abstention before verification");
}

void test_upstream_status_and_exactness_are_not_coerced() {
    auto request = base_request();
    request.upstream_status = Status::timeout;
    auto output = decide(request, base_policy());
    require(output.status == DecisionStatus::abstention &&
                output.upstream_status == Status::timeout && !output.action_id,
            "timeout must remain visible and cannot become a selected answer");

    request = base_request();
    request.output_exactness = Exactness::approximate;
    output = decide(request, base_policy());
    require(output.status == DecisionStatus::abstention &&
                output.exactness == Exactness::not_applicable,
            "exact-WMC policy must abstain rather than present approximate output as exact");

    request = base_request();
    request.upstream_status = static_cast<Status>(999);
    output = decide(request, base_policy());
    require(output.status == DecisionStatus::invalid_input &&
                output.upstream_status == Status::invalid_schema &&
                !serialize(output).empty(),
            "invalid status enums must become a serializable explicit schema failure");
}

void test_canonical_output_and_resource_normalization() {
    auto request = base_request();
    request.actions[0].loss_by_state = {{"exact", Rational{0}},
                                        {"wrong_or_incomplete", Rational{0}}};
    TestVerifier verifier;
    const auto output = decide(request, base_policy(), &verifier);
    const auto json = serialize(output);
    require(json.find("\"action_id\":\"wmc.emit_exact_result\"") != std::string::npos &&
                json.find("\"action_value\":{\"$rational\":[\"3\",\"7\"]}") != std::string::npos &&
                json.find("\"expected_risk\":{\"$rational\":[\"1\",\"8\"]}") != std::string::npos &&
                json.find("\"certificate_status\":\"verified\"") != std::string::npos,
            "structured output must expose exact risk and certificate status");
    require(serialize(output) == json, "decision serialization must be deterministic");
    auto malformed = output;
    malformed.action_id.reset();
    bool schema_rejected = false;
    try {
        static_cast<void>(serialize(malformed));
    } catch (const std::invalid_argument&) {
        schema_rejected = true;
    }
    require(schema_rejected, "serializer must reject outcome/schema mismatch");

    require(normalized_wmc_resource_cost(300'000, kWmcMemoryBudgetBytes / 4) == fraction(1, 2),
            "resource cost must be the maximum of normalized CPU and RAM fractions");
    require(normalized_wmc_resource_cost(kWmcCpuBudgetMs + 1, 0) > 1,
            "normalized resource cost above one must expose a hard-budget overrun");
}

}  // namespace

int main() {
    try {
        test_exact_risk_ordering_and_certificate_gate();
        std::cout << "PASS exact_risk_ordering: rational expected risk and certificate-first eligibility\n";
        test_risk_ties_and_abstention_tie_precedence();
        std::cout << "PASS ties: deterministic action order and abstention-on-tie\n";
        test_invalid_probability_and_unavailable_loss();
        std::cout << "PASS malformed_inputs: mass, loss coverage, uniqueness, and request bounds\n";
        test_missing_stale_and_mismatched_certificates();
        std::cout << "PASS certificate_bindings: absent, malformed, stale, and mismatched certificates fail closed\n";
        test_verifier_failure_and_exception();
        std::cout << "PASS verifier_failures: rejection and exceptions exclude actions\n";
        test_no_certified_action_resource_and_unresolved_abstention();
        std::cout << "PASS abstention: no-certified-action, over-budget, and unresolved-state paths\n";
        test_upstream_status_and_exactness_are_not_coerced();
        std::cout << "PASS status_propagation: timeout and approximate results cannot become exact answers\n";
        test_canonical_output_and_resource_normalization();
        std::cout << "PASS output_schema: exact-rational value/risk output and WMC resource normalization\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL decision_tests: " << error.what() << '\n';
        return 1;
    }
}
