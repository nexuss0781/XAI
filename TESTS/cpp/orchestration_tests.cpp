#include "xai/orchestration.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace xai;
using namespace xai::orchestration;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] contracts::CanonicalValue rational_value(const std::string& numerator,
                                                        const std::string& denominator) {
    return contracts::CanonicalValue{contracts::CanonicalValue::Object{{"$rational",
        contracts::CanonicalValue::Array{contracts::CanonicalValue{numerator},
                                         contracts::CanonicalValue{denominator}}}}};
}

[[nodiscard]] contracts::CanonicalValue expected_result_value() {
    return contracts::CanonicalValue{contracts::CanonicalValue::Object{
        {"count", rational_value("3", "1")},
        {"satisfiable", contracts::CanonicalValue{true}},
        {"status", contracts::CanonicalValue{"success"}},
        {"solver_version", contracts::CanonicalValue{std::string(kSolverVersion)}}}};
}

[[nodiscard]] StructuredWmcInput basic_input() {
    StructuredWmcInput input;
    input.observation_id = contracts::ObservationId{"observation:phase8-fixture"};
    input.source_id = contracts::SourceId{"source:phase8-fixture"};
    input.partition_id = contracts::PartitionId{std::string(kPartitionId)};
    input.partition = contracts::DataPartition::development;
    input.original_input_ref = "fixture:phase8/simple.cnf";
    input.observed_at_utc = "2026-10-07T00:00:00Z";
    input.extractor_version = std::string(kParserVersion);
    input.formula_text = "p cnf 2 1\n1 2 0\n";
    input.uncertainty_codes = {"uncertainty:formal-id-only"};
    return input;
}

class FixtureVerifier final : public decision::CertificateVerifier {
public:
    bool accept{true};
    [[nodiscard]] std::string_view id() const noexcept override { return "test.phase8.wmc-verifier"; }
    [[nodiscard]] std::string_view version() const noexcept override { return "1"; }
    [[nodiscard]] decision::VerificationResult verify(
        const decision::Certificate& certificate, std::string_view expected_property_id,
        std::string_view expected_subject_sha256, std::string_view expected_claim_sha256,
        const contracts::CanonicalValue& output_value) const override {
        const bool bindings = certificate.property_id == expected_property_id &&
            certificate.subject_sha256 == expected_subject_sha256 &&
            certificate.claim_sha256 == expected_claim_sha256 &&
            output_value == expected_result_value();
        return {accept && bindings, accept && bindings ? std::string(expected_property_id) : "",
                "synthetic Phase 8 fixture verifier; not a production proof verifier"};
    }
};

[[nodiscard]] RunOptions certified_options(const StructuredWmcInput& input) {
    RunOptions options;
    options.run_id = contracts::RunId{"run:phase8-success"};
    options.code_build_id = contracts::CodeBuildId{"phase8-test-build"};
    options.decision_policy.decision_time_epoch = 50;
    const auto subject = sha256_hex(input.formula_text);
    const auto claim = sha256_hex(contracts::encode_canonical_value(expected_result_value()));
    options.certificate = decision::Certificate{
        contracts::CertificateId{"certificate:phase8-success"}, "wmc.emit_exact_result",
        subject, claim, std::string(decision::kWmcExactResultProperty),
        "test.phase8.wmc-verifier", "1", 10, 100};
    return options;
}

[[nodiscard]] ingestion::EvidenceRecord hard_evidence(
    std::string evidence_id, std::string source_id,
    std::vector<ingestion::Rational> likelihood) {
    ingestion::EvidenceRecord evidence;
    evidence.evidence_id = contracts::EvidenceId{std::move(evidence_id)};
    evidence.source_id = contracts::SourceId{std::move(source_id)};
    evidence.observed_at_utc = "2026-10-07T00:00:00Z";
    evidence.original_observation_ref = "fixture:phase8/evidence";
    evidence.extractor_version = "phase8-test-input-v1";
    evidence.partition = contracts::DataPartition::development;
    evidence.scope = {contracts::FactId{"var:1"}};
    evidence.semantics = ingestion::EvidenceSemantics::hard_constraint;
    evidence.likelihood = std::move(likelihood);
    evidence.dependence = ingestion::DependenceClass::independent;
    return evidence;
}

[[nodiscard]] adaptation::DevelopmentSuite adaptation_suite() {
    std::istringstream formula("p cnf 2 1\n1 2 0\n");
    adaptation::DevelopmentCase item;
    item.observation_id = contracts::ObservationId{"adapt:dev-case"};
    item.partition = contracts::DataPartition::development;
    item.instance = wmc::parse_dimacs_wmc(formula);
    item.expected_count = wmc::Rational{3};
    item.expected_satisfiable = true;
    return {"phase8-dev-fixture", contracts::PartitionId{"phase8-development"},
            contracts::DataPartition::development, {item}};
}

void test_sha256_vectors() {
    require(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 empty-message vector mismatch");
    require(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 abc vector mismatch");
    const std::string million_as(1'000'000, 'a');
    require(sha256_hex(million_as) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
            "SHA-256 million-byte multi-block vector mismatch");
}

void test_successful_traced_run_and_manifest() {
    auto input = basic_input();
    auto options = certified_options(input);
    FixtureVerifier verifier;
    const auto result = execute(input, options, &verifier);
    require(result.status == contracts::Status::success && result.exactness == contracts::Exactness::exact,
            "valid structured public-development input should complete with the fixture verifier");
    require(result.exact_count == std::optional<wmc::Rational>(wmc::Rational{3}) &&
                result.satisfiable == std::optional<bool>(true),
            "exact WMC result does not match the fixture");
    require(result.decision_output && result.decision_output->status == decision::DecisionStatus::selected &&
                result.decision_output->action_id == std::optional<std::string>("wmc.emit_exact_result"),
            "certificate-gated decision did not select the exact result");
    require(result.factual_model_sha256 && result.factual_query_status == std::nullopt,
            "factual finite model should be recorded without inventing an unrequested query");
    require(result.manifest.modules.size() == 8,
            "all required and skipped modules should be represented in the trace");
    require(result.manifest.code_sha256.size() == 64 && result.manifest.data_sha256.size() == 64 &&
                result.manifest.model_sha256.size() == 64 && result.manifest.config_sha256.size() == 64 &&
                result.manifest.supplemental_input_sha256.size() == 64 &&
                result.manifest.input_schema_sha256.size() == 64 && result.manifest.run_schema_sha256.size() == 64 &&
                result.manifest.output_schema_sha256.size() == 64 &&
                result.manifest.parser_version == kParserVersion &&
                result.manifest.extractor_version == input.extractor_version,
            "run manifest must carry build, schema, data, model, and configuration SHA-256 hashes");
    const auto serialized = serialize(result);
    require(serialized.find("p cnf") == std::string::npos &&
                serialized.find("fixture:phase8/simple.cnf") != std::string::npos &&
                serialized.find("calibration") != std::string::npos &&
                serialized.find("uncertainty:formal-id-only") != std::string::npos,
            "report should retain the opaque source reference and skip rationale but not raw input");
    require(serialized.find("wmc.emit_exact_result") != std::string::npos &&
                serialized.find("recursive_nodes") != std::string::npos,
            "canonical manifest should include decision and resource trace fields");
}

void test_locked_partition_rejected_before_content_access() {
    auto input = basic_input();
    input.partition = contracts::DataPartition::final_test;
    input.partition_id = contracts::PartitionId{"locked-final"};
    input.formula_text = "malformed secret holdout body should not be parsed";
    const auto result = execute(input);
    require(result.status == contracts::Status::unsupported_input && result.manifest.data_sha256.empty() &&
                result.manifest.input_bytes == 0,
            "final-test partition must be rejected before hashing or reading the formula body");
    require(result.input_diagnostic.find("public-even") != std::string::npos,
            "partition rejection should be explicit");
}

void test_missing_fact_and_contradictory_evidence_fail_closed() {
    auto missing = basic_input();
    missing.query_fact_id = contracts::FactId{"fact:not-present"};
    auto result = execute(missing);
    require(result.status == contracts::Status::unknown &&
                result.factual_query_status == std::optional<contracts::Status>(contracts::Status::unknown) &&
                !result.exact_count,
            "open-world missing fact must remain unknown rather than becoming false or success");

    auto contradiction = basic_input();
    contradiction.additional_evidence.push_back(hard_evidence("evidence:positive", "source:a",
        {wmc::Rational{0}, wmc::Rational{1}}));
    contradiction.additional_evidence.push_back(hard_evidence("evidence:negative", "source:b",
        {wmc::Rational{1}, wmc::Rational{0}}));
    contradiction.query_fact_id = contracts::FactId{"var:1"};
    result = execute(contradiction);
    require(result.status == contracts::Status::inconsistent &&
                result.factual_query_status == std::optional<contracts::Status>(contracts::Status::inconsistent) &&
                !result.exact_count,
            "contradictory factual evidence must preserve inconsistent status and block confident output");
}

void test_correlated_evidence_is_not_multiplied() {
    auto input = basic_input();
    auto root = hard_evidence("evidence:root", "source:root", {wmc::Rational{1}, wmc::Rational{2}});
    root.semantics = ingestion::EvidenceSemantics::likelihood;
    root.dependence = ingestion::DependenceClass::independent;
    input.additional_evidence.push_back(root);
    auto related = root;
    related.evidence_id = contracts::EvidenceId{"evidence:correlated"};
    related.source_id = contracts::SourceId{"source:related"};
    related.dependence = ingestion::DependenceClass::correlated;
    related.dependencies = {{root.evidence_id, ingestion::DependencyRelation::correlated}};
    input.additional_evidence.push_back(related);
    input.query_fact_id = contracts::FactId{"var:1"};
    const auto result = execute(input);
    require(result.status == contracts::Status::unsupported_input &&
                result.factual_query_status == std::optional<contracts::Status>(contracts::Status::unsupported_input),
            "correlated evidence must not be silently treated as independent");
}

void test_ambiguous_identity_and_unsupported_input() {
    auto input = basic_input();
    ingestion::IdentityReference ambiguous;
    ambiguous.resolution = ingestion::IdentityResolution::unresolved;
    ambiguous.candidate_entity_ids = {contracts::EntityId{"candidate:a"}, contracts::EntityId{"candidate:b"}};
    input.identity_bindings.push_back({1, ambiguous, true});
    auto result = execute(input);
    require(result.status == contracts::Status::unsupported_input &&
                result.manifest.identity_bindings.size() == 1 &&
                result.manifest.identity_bindings.front().identity.candidate_entity_ids.size() == 2,
            "required ambiguous identity must be preserved and rejected, not guessed");

    input = basic_input();
    input.formula_text = "c t pwmc\np cnf 1 0\n";
    result = execute(input);
    require(result.status == contracts::Status::unsupported_input && !result.exact_count,
            "projected WMC must remain explicitly unsupported");

    input = basic_input();
    constexpr std::string_view private_token = "SECRET_WEIGHT_TOKEN_987";
    input.formula_text = "c p weight 1 SECRET_WEIGHT_TOKEN_987 0\np cnf 1 0\n";
    result = execute(input);
    const auto serialized = serialize(result);
    require(result.status == contracts::Status::invalid_schema &&
                result.input_diagnostic.find(private_token) == std::string::npos &&
                serialized.find(private_token) == std::string::npos,
            "parser errors must not copy untrusted formula tokens into diagnostics or reports");

    input = basic_input();
    input.rejected_span_refs = {"span:unsupported-feature"};
    result = execute(input);
    require(result.status == contracts::Status::unsupported_input &&
                result.manifest.rejected_span_refs == input.rejected_span_refs && !result.exact_count,
            "caller-declared unsupported content must be retained by reference and block partial mapping");
}

void test_solver_timeout_has_no_partial_count() {
    auto input = basic_input();
    RunOptions options;
    options.solver_options.timeout_ms = 0;
    const auto result = execute(input, options);
    require(result.status == contracts::Status::timeout && !result.exact_count &&
                result.manifest.modules.back().status == contracts::Status::timeout,
            "expired solver deadline must return timeout without a partial count");
}

void test_shift_alarm_abstains_and_audit_is_retained() {
    auto input = basic_input();
    RunOptions options;
    AdaptationScenario scenario;
    scenario.development_suite = adaptation_suite();
    scenario.controller_options.detector.mu_in_control = 0.0;
    scenario.controller_options.detector.mu_shift = 0.001;
    scenario.controller_options.detector.sigma = 0.01;
    scenario.controller_options.detector.threshold = 0.001;
    scenario.controller_options.detector.sample_cadence = 1;
    scenario.controller_options.detector.hold_off_samples = 2;
    scenario.streaming_observation = {contracts::ObservationId{"stream:phase8-shift"},
        contracts::PartitionId{"phase8-stream"}, contracts::DataPartition::streaming, true, 4};
    options.adaptation = scenario;
    const auto result = execute(input, options);
    require(result.status == contracts::Status::alarm_frozen && !result.exact_count && !result.satisfiable &&
                result.manifest.solver_limits.node_limit == 128 &&
                result.decision_output && result.decision_output->status == decision::DecisionStatus::abstention,
            "shift alarm must remain visible and prevent promotion to selected output");
    require(!result.manifest.adaptation_audit.empty(),
            "adaptation transition audit should not be empty");
    require(result.manifest.adaptation_audit.back().kind == adaptation::EventKind::shift_alarm_rollback,
            "adaptation monitor should emit a shift-alarm rollback event");
    require(serialize(result).find("shift_alarm_rollback") != std::string::npos,
            "serialized run manifest should name the shift-alarm rollback event");
}

void test_certificate_rejection_and_missing_certificate_abstain() {
    auto input = basic_input();
    auto options = certified_options(input);
    options.certificate->subject_sha256 = std::string(64, 'f');
    FixtureVerifier verifier;
    auto result = execute(input, options, &verifier);
    require(result.status == contracts::Status::abstention && result.decision_output &&
                result.decision_output->status == decision::DecisionStatus::abstention &&
                !result.exact_count && !result.satisfiable,
            "mismatched certificate must not authorize an exact output");

    options = RunOptions{};
    result = execute(input, options);
    require(result.status == contracts::Status::abstention && result.decision_output &&
                result.decision_output->assessments.front().certificate_status == decision::CertificateStatus::missing &&
                !result.exact_count && !result.satisfiable &&
                serialize(result).find("\"exact_count\":null") != std::string::npos,
            "missing certificate/verifier must leave explicit abstention available");

    options = RunOptions{};
    options.memory_budget_bytes = 1;
    result = execute(input, options);
    require(result.status == contracts::Status::abstention && result.decision_output &&
                result.decision_output->assessments.front().certificate_status == decision::CertificateStatus::resource_limit &&
                !result.exact_count && !result.satisfiable,
            "measured peak RSS above a caller budget must exclude the result action");
}

void test_replay_and_manifest_determinism() {
    auto input = basic_input();
    const auto options = certified_options(input);
    FixtureVerifier verifier;
    const auto first = execute(input, options, &verifier);
    const auto second = execute(input, options, &verifier);
    require(first.semantic_sha256 == second.semantic_sha256 && first.status == second.status &&
                first.exact_count == second.exact_count,
            "same versioned input/configuration must replay to the same semantic result");
    require(first.manifest.resources.elapsed_wall_ms <= 600'000 &&
                first.manifest.resources.solver_recursive_nodes > 0,
            "run manifest should expose timing and solver-resource use");
}

}  // namespace

int main() {
    try {
        test_sha256_vectors();
        std::cout << "PASS sha256: standard empty and abc test vectors\n";
        test_successful_traced_run_and_manifest();
        std::cout << "PASS success_manifest: structured mapping, exact WMC, certificate gate, hashes, and skipped-module rationale\n";
        test_locked_partition_rejected_before_content_access();
        std::cout << "PASS partition_isolation: final-test rejection before data hashing/parsing\n";
        test_missing_fact_and_contradictory_evidence_fail_closed();
        std::cout << "PASS factual_status: open-world unknown and inconsistent evidence propagate unchanged\n";
        test_correlated_evidence_is_not_multiplied();
        std::cout << "PASS dependence: correlated evidence remains unsupported\n";
        test_ambiguous_identity_and_unsupported_input();
        std::cout << "PASS mapping_fail_closed: ambiguous identity and projected input are explicit\n";
        test_solver_timeout_has_no_partial_count();
        std::cout << "PASS timeout: expired deadline returns no partial count\n";
        test_shift_alarm_abstains_and_audit_is_retained();
        std::cout << "PASS adaptation_alarm: shift status, rollback audit, and abstention propagate\n";
        test_certificate_rejection_and_missing_certificate_abstain();
        std::cout << "PASS certificate_gate: invalid/missing certificates fail closed to abstention\n";
        test_replay_and_manifest_determinism();
        std::cout << "PASS replay: stable semantic digest with timing tracked separately\n";
        std::cout << "RESULT: 10/10 Phase 8 end-to-end harness groups passed; synthetic fixtures only.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
