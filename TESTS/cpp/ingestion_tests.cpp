#include "xai/ingestion.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace xai::contracts;
using namespace xai::ingestion;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
Rational q(const std::string& text) {
    Rational result;
    if (result.set_str(text, 10) != 0) throw std::runtime_error("bad test rational: " + text);
    result.canonicalize();
    return result;
}
FactDefinition resolved_fact(const std::string& id, const std::string& entity) {
    return {FactId{id}, "wmc.variable", IdentityReference{
        IdentityResolution::resolved, EntityId{entity}, {}}};
}
FactDefinition unresolved_fact(const std::string& id) {
    return {FactId{id}, "wmc.variable", IdentityReference{
        IdentityResolution::unresolved, std::nullopt, {EntityId{"candidate:a"}, EntityId{"candidate:b"}}}};
}
ModelDefinition two_fact_definition(ResourceLimits limits = {}) {
    ModelDefinition definition;
    definition.run_id = RunId{"run-phase2"};
    definition.model_id = ModelId{"model-phase2"};
    definition.code_build_id = CodeBuildId{"git:phase2"};
    definition.partition_id = PartitionId{"mcc24-public-even"};
    definition.partition = DataPartition::development;
    definition.facts = {resolved_fact("fact:x", "formula:instance-1"), unresolved_fact("fact:y")};
    definition.priors = {{FactId{"fact:x"}, q("4/5"), q("1/5")},
                         {FactId{"fact:y"}, q("3/10"), q("7/10")}};
    definition.limits = limits;
    return definition;
}
EvidenceRecord clause_evidence(std::string id = "evidence:clause") {
    EvidenceRecord evidence;
    evidence.evidence_id = EvidenceId{std::move(id)};
    evidence.source_id = SourceId{"source:formula"};
    evidence.observed_at_utc = "2026-10-07T00:00:00Z";
    evidence.original_observation_ref = "sha256:fixture-formula-01";
    evidence.extractor_version = "dimacs-parser-v1";
    evidence.partition = DataPartition::development;
    evidence.scope = {FactId{"fact:x"}, FactId{"fact:y"}};
    evidence.semantics = EvidenceSemantics::hard_constraint;
    evidence.likelihood = {q("0"), q("1"), q("1"), q("1")};
    evidence.dependence = DependenceClass::model_factor;
    return evidence;
}
EvidenceRecord unary_evidence(std::string id, std::vector<Rational> values,
                              EvidenceSemantics semantics = EvidenceSemantics::likelihood) {
    EvidenceRecord evidence;
    evidence.evidence_id = EvidenceId{std::move(id)};
    evidence.source_id = SourceId{"source:sensor"};
    evidence.observed_at_utc = "2026-10-07T01:02:03Z";
    evidence.original_observation_ref = "urn:fixture:observation:17";
    evidence.extractor_version = "extractor-1.2.0";
    evidence.partition = DataPartition::development;
    evidence.scope = {FactId{"fact:x"}};
    evidence.semantics = semantics;
    evidence.likelihood = std::move(values);
    evidence.dependence = DependenceClass::independent;
    return evidence;
}

void test_prior_updates_and_exact_wmc_semantics() {
    BeliefModel model{two_fact_definition()};
    const auto prior = model.query(FactId{"fact:x"});
    require(prior.status == Status::success && prior.probability_true == q("1/5") &&
                prior.prior_normalizer == q("1") && prior.evidence_normalizer == q("1"),
            "prior marginal or initial normalizer is incorrect");
    const auto evidence = clause_evidence();
    require(model.add_evidence(evidence).accepted(), "valid hard-constraint evidence was rejected");
    auto caller_copy = evidence;
    caller_copy.likelihood[0] = q("1");
    require(model.evidence().front().likelihood[0] == q("0"),
            "ledger evidence changed after insertion");
    const auto updated = model.query(FactId{"fact:x"});
    require(updated.status == Status::success && updated.evidence_normalizer == q("19/25") &&
                updated.conditional_normalizer == q("19/25") &&
                updated.probability_true == q("5/19"),
            "exact constrained marginal or normalizer is incorrect");
    const auto record = model.query_record(QueryId{"query:x"}, ResultId{"result:x"}, FactId{"fact:x"});
    require(validate_record(record).empty() && record.status == Status::success &&
                record.exactness == Exactness::exact,
            "successful marginal did not produce a valid versioned result record");

    ModelDefinition unweighted;
    unweighted.run_id = RunId{"run-unweighted"};
    unweighted.model_id = ModelId{"model-unweighted"};
    unweighted.code_build_id = CodeBuildId{"git:phase2"};
    unweighted.partition_id = PartitionId{"mcc24-public-even"};
    unweighted.facts = {resolved_fact("fact:u", "formula:unweighted")};
    unweighted.priors = {{FactId{"fact:u"}, q("1"), q("1")}};
    BeliefModel wmc_model{std::move(unweighted)};
    auto unit = unary_evidence("evidence:unit", {q("0"), q("1")}, EvidenceSemantics::hard_constraint);
    unit.scope = {FactId{"fact:u"}};
    require(wmc_model.add_evidence(std::move(unit)).accepted(), "unweighted unit constraint rejected");
    const auto wmc = wmc_model.query(FactId{"fact:u"});
    require(wmc.status == Status::success && wmc.prior_normalizer == q("2") &&
                wmc.evidence_normalizer == q("1") && wmc.conditional_normalizer == q("1/2") &&
                wmc.probability_true == q("1"),
            "unnormalized (1,1) WMC potentials or partition function are incorrect");
}

void test_likelihood_update_and_zero_normalizers() {
    BeliefModel model{two_fact_definition()};
    require(model.add_evidence(unary_evidence("evidence:soft", {q("1/4"), q("3/4")})).accepted(),
            "valid soft likelihood evidence was rejected");
    const auto posterior = model.query(FactId{"fact:x"});
    require(posterior.status == Status::success && posterior.evidence_normalizer == q("7/20") &&
                posterior.probability_true == q("3/7"),
            "exact likelihood update is incorrect");

    auto zero_prior = two_fact_definition();
    zero_prior.priors[0].false_weight = q("0");
    zero_prior.priors[0].true_weight = q("0");
    const auto zero_prior_result = BeliefModel{std::move(zero_prior)}.query(FactId{"fact:x"});
    require(zero_prior_result.status == Status::zero_normalizer && !zero_prior_result.probability_true,
            "zero prior normalizer did not fail explicitly");

    BeliefModel zero_soft_model{two_fact_definition()};
    require(zero_soft_model.add_evidence(unary_evidence("evidence:zero", {q("0"), q("0")})).accepted(),
            "well-formed zero likelihood table rejected at ingestion");
    const auto zero_soft_result = zero_soft_model.query(FactId{"fact:x"});
    require(zero_soft_result.status == Status::zero_normalizer &&
                zero_soft_result.evidence_normalizer == q("0") && !zero_soft_result.probability_true,
            "zero likelihood normalizer was not distinguished explicitly");
}

void test_contradictions_unknown_and_unresolved_identity() {
    BeliefModel model{two_fact_definition()};
    auto positive = unary_evidence("evidence:positive", {q("0"), q("1")}, EvidenceSemantics::hard_constraint);
    auto negative = unary_evidence("evidence:negative", {q("1"), q("0")}, EvidenceSemantics::hard_constraint);
    require(model.add_evidence(std::move(positive)).accepted() && model.add_evidence(std::move(negative)).accepted(),
            "valid but mutually contradictory evidence was rejected before inference");
    const auto contradiction = model.query(FactId{"fact:x"});
    require(contradiction.status == Status::inconsistent && contradiction.evidence_normalizer == q("0") &&
                !contradiction.probability_true,
            "contradictory evidence did not yield an explicit inconsistent status");
    const auto absent = model.query(FactId{"fact:not-present"});
    require(absent.status == Status::unknown && !absent.probability_true,
            "absent fact was treated as false instead of unknown");
    const auto failure_record = model.query_record(QueryId{"query:absent"}, ResultId{"result:absent"},
                                                    FactId{"fact:not-present"});
    require(validate_record(failure_record).empty() && failure_record.status == Status::unknown &&
                !failure_record.result,
            "unknown query did not preserve the shared failure contract");
    const auto& identity = model.definition().facts[1].subject;
    require(identity.resolution == IdentityResolution::unresolved && !identity.entity_id &&
                identity.candidate_entity_ids.size() == 2,
            "unresolved entity identity was collapsed or discarded");
    BeliefModel identity_only_model{two_fact_definition()};
    const auto unresolved_query = identity_only_model.query(FactId{"fact:y"});
    const auto unresolved_record = identity_only_model.query_record(
        QueryId{"query:unresolved"}, ResultId{"result:unresolved"}, FactId{"fact:y"});
    bool identity_limitation_recorded = false;
    for (const auto& assumption : unresolved_record.assumptions)
        if (assumption.find("unresolved entity identity") != std::string::npos)
            identity_limitation_recorded = true;
    require(unresolved_query.status == Status::success && unresolved_query.probability_true == q("7/10") &&
                !unresolved_record.identity.entity_id && identity_limitation_recorded,
            "identity candidates were silently treated as resolved equality or lost in query provenance");
}

void test_duplicate_correlated_and_unresolved_dependence() {
    BeliefModel duplicate_model{two_fact_definition()};
    auto original = unary_evidence("evidence:original", {q("0"), q("1")});
    require(duplicate_model.add_evidence(original).accepted(), "first independent evidence rejected");
    auto duplicate = original;
    duplicate.evidence_id = EvidenceId{"evidence:duplicate"};
    duplicate.source_id = SourceId{"source:mirror"};
    duplicate.original_observation_ref = "urn:fixture:duplicate-copy";
    duplicate.dependence = DependenceClass::exact_duplicate;
    duplicate.dependencies = {{EvidenceId{"evidence:original"}, DependencyRelation::exact_duplicate}};
    require(duplicate_model.add_evidence(std::move(duplicate)).accepted(), "linked exact duplicate was rejected");
    const auto deduplicated = duplicate_model.query(FactId{"fact:x"});
    require(deduplicated.status == Status::success && deduplicated.evidence_normalizer == q("1/5") &&
                deduplicated.probability_true == q("1") && deduplicated.evidence_lineage.size() == 2,
            "exact duplicate was counted twice or omitted from provenance");

    BeliefModel correlated_model{two_fact_definition()};
    require(correlated_model.add_evidence(unary_evidence("evidence:root", {q("1"), q("2")})).accepted(),
            "first correlated-source fixture rejected");
    auto correlated = unary_evidence("evidence:related", {q("1"), q("2")});
    correlated.dependence = DependenceClass::correlated;
    correlated.dependencies = {{EvidenceId{"evidence:root"}, DependencyRelation::correlated}};
    require(correlated_model.add_evidence(std::move(correlated)).accepted(), "linked correlated source was rejected");
    const auto blocked = correlated_model.query(FactId{"fact:x"});
    require(blocked.status == Status::unsupported_input && !blocked.probability_true,
            "correlated evidence was silently multiplied as independent");

    BeliefModel unknown_model{two_fact_definition()};
    auto unknown_evidence = unary_evidence("evidence:unknown-dependence", {q("1"), q("2")});
    unknown_evidence.dependence = DependenceClass::unknown;
    require(unknown_model.add_evidence(std::move(unknown_evidence)).accepted(),
            "evidence with explicitly unknown dependence was rejected");
    const auto unknown = unknown_model.query(FactId{"fact:x"});
    require(unknown.status == Status::unsupported_input,
            "unknown dependence was silently treated as independence");

    BeliefModel invalid_duplicate_model{two_fact_definition()};
    require(invalid_duplicate_model.add_evidence(unary_evidence("evidence:base", {q("0"), q("1")})).accepted(),
            "duplicate validation fixture could not add its base factor");
    auto mismatched = unary_evidence("evidence:mismatched", {q("1"), q("0")});
    mismatched.dependence = DependenceClass::exact_duplicate;
    mismatched.dependencies = {{EvidenceId{"evidence:base"}, DependencyRelation::exact_duplicate}};
    require(invalid_duplicate_model.add_evidence(std::move(mismatched)).status == Status::invalid_schema,
            "factor-mismatched duplicate claim was accepted");
}

void test_validation_partition_and_resource_limits() {
    BeliefModel model{two_fact_definition()};
    auto malformed = unary_evidence("evidence:bad-time", {q("1"), q("1")});
    malformed.observed_at_utc = "2026-02-30T00:00:00Z";
    require(model.add_evidence(std::move(malformed)).status == Status::invalid_schema,
            "invalid UTC timestamp accepted");
    auto wrong_partition = unary_evidence("evidence:wrong-partition", {q("1"), q("1")});
    wrong_partition.partition = DataPartition::final_test;
    require(model.add_evidence(std::move(wrong_partition)).status == Status::invalid_schema,
            "evidence from a different/locked partition entered the model");
    auto bad_constraint = unary_evidence("evidence:bad-constraint", {q("1/2"), q("1")},
                                         EvidenceSemantics::hard_constraint);
    require(model.add_evidence(std::move(bad_constraint)).status == Status::invalid_schema,
            "non-Boolean hard-constraint likelihood accepted");

    auto state_limits = ResourceLimits{};
    state_limits.max_facts = 2;
    state_limits.max_worlds = 2;
    BeliefModel state_limited{two_fact_definition(state_limits)};
    require(state_limited.query(FactId{"fact:x"}).status == Status::resource_limit,
            "state-count exhaustion did not return resource_limit");
    auto operation_limits = ResourceLimits{};
    operation_limits.max_facts = 2;
    operation_limits.max_worlds = 4;
    operation_limits.max_operations = 1;
    BeliefModel operation_limited{two_fact_definition(operation_limits)};
    const auto operation_result = operation_limited.query(FactId{"fact:x"});
    require(operation_result.status == Status::resource_limit && !operation_result.probability_true,
            "operation exhaustion returned a partial marginal");
    auto evidence_limits = ResourceLimits{};
    evidence_limits.max_facts = 2;
    evidence_limits.max_evidence = 1;
    BeliefModel evidence_limited{two_fact_definition(evidence_limits)};
    require(evidence_limited.add_evidence(unary_evidence("evidence:first", {q("1"), q("1")})).accepted() &&
                evidence_limited.add_evidence(unary_evidence("evidence:second", {q("1"), q("1")})).status ==
                    Status::resource_limit,
            "evidence-count limit was not enforced explicitly");

    auto factor_limits = ResourceLimits{};
    factor_limits.max_facts = 2;
    factor_limits.max_worlds = 2;
    BeliefModel factor_limited{two_fact_definition(factor_limits)};
    require(factor_limited.add_evidence(clause_evidence()).status == Status::resource_limit,
            "direct evidence table larger than max_worlds was accepted");

    auto aggregate_limits = ResourceLimits{};
    aggregate_limits.max_facts = 2;
    aggregate_limits.max_factor_entries = 2;
    BeliefModel aggregate_limited{two_fact_definition(aggregate_limits)};
    require(aggregate_limited.add_evidence(unary_evidence("evidence:factor-one", {q("1"), q("1")})).accepted() &&
                aggregate_limited.add_evidence(unary_evidence("evidence:factor-two", {q("1"), q("1")})).status ==
                    Status::resource_limit,
            "aggregate factor-table storage cap was not enforced");

    auto rational_limits = ResourceLimits{};
    rational_limits.max_facts = 2;
    rational_limits.max_rational_bits = 8;
    BeliefModel rational_limited{two_fact_definition(rational_limits)};
    require(rational_limited.add_evidence(unary_evidence("evidence:large-rational",
                {q("1/256"), q("1")})).status == Status::resource_limit,
            "evidence rational above the configured bit ceiling was accepted");
    auto oversized_prior = two_fact_definition(rational_limits);
    oversized_prior.priors[0].false_weight = q("1/256");
    bool prior_rejected = false;
    try { BeliefModel rejected{std::move(oversized_prior)}; (void)rejected; }
    catch (const std::invalid_argument&) { prior_rejected = true; }
    require(prior_rejected, "prior rational above the configured bit ceiling was accepted");

    auto accumulator_limits = ResourceLimits{};
    accumulator_limits.max_facts = 4;
    accumulator_limits.max_worlds = 16;
    accumulator_limits.max_rational_bits = 4;
    ModelDefinition accumulator_definition;
    accumulator_definition.run_id = RunId{"run-accumulator-limit"};
    accumulator_definition.model_id = ModelId{"model-accumulator-limit"};
    accumulator_definition.code_build_id = CodeBuildId{"git:phase2"};
    accumulator_definition.partition_id = PartitionId{"fixture-development"};
    accumulator_definition.limits = accumulator_limits;
    for (int i = 0; i < 4; ++i) {
        const auto id = "fact:acc" + std::to_string(i);
        accumulator_definition.facts.push_back(resolved_fact(id, "formula:accumulator"));
        accumulator_definition.priors.push_back({FactId{id}, q("1/3"), q("2/3")});
    }
    const auto accumulator_result = BeliefModel{std::move(accumulator_definition)}.query(FactId{"fact:acc0"});
    require(accumulator_result.status == Status::resource_limit && !accumulator_result.probability_true,
            "exact-rational intermediate growth escaped its configured bit ceiling");
}

void test_canonical_snapshot_replay_and_lineage() {
    BeliefModel original{two_fact_definition()};
    require(original.add_evidence(clause_evidence()).accepted(), "replay fixture evidence rejected");
    auto duplicate = original.evidence().front();
    duplicate.evidence_id = EvidenceId{"evidence:replay-duplicate"};
    duplicate.dependence = DependenceClass::exact_duplicate;
    duplicate.dependencies = {{EvidenceId{"evidence:clause"}, DependencyRelation::exact_duplicate}};
    require(original.add_evidence(std::move(duplicate)).accepted(), "replay duplicate rejected");
    const auto serialized = original.serialize();
    const auto replayed = BeliefModel::restore(serialized);
    require(replayed.serialize() == serialized && replayed.evidence().size() == 2 &&
                replayed.definition().facts[1].subject.resolution == IdentityResolution::unresolved,
            "canonical snapshot did not restore evidence, limits, and identity state exactly");
    const auto before = original.query_record(QueryId{"query:replay"}, ResultId{"result:replay"}, FactId{"fact:x"});
    const auto after = replayed.query_record(QueryId{"query:replay"}, ResultId{"result:replay"}, FactId{"fact:x"});
    require(encode_record(before) == encode_record(after),
            "replayed model did not reproduce byte-identical exact query records");
    require(before.lineage.size() == 9 && before.identity.model_id == original.definition().model_id &&
                before.identity.fact_id == FactId{"fact:x"},
            "query record omitted evidence/source/fact/entity/schema/build/model lineage");
    require(std::holds_alternative<DecodeFailure>(decode_record(serialized,
                SchemaExpectation{SchemaId{"xai.other"}, 1})),
            "snapshot schema mismatch was silently accepted");
    require(std::holds_alternative<DecodeFailure>(decode_record(serialized.substr(0, serialized.size() - 1))),
            "truncated model snapshot was accepted");
    auto huge_rational = serialized;
    const std::string small_pair = "\"$rational\":[\"4\",\"5\"]";
    const auto rational_position = huge_rational.find(small_pair);
    require(rational_position != std::string::npos, "snapshot rational fixture not found");
    const std::string large_pair = "\"$rational\":[\"" + std::string(10'000, '9') + "\",\"5\"]";
    huge_rational.replace(rational_position, small_pair.size(), large_pair);
    bool large_rational_rejected = false;
    try { (void)BeliefModel::restore(huge_rational); }
    catch (const std::invalid_argument&) { large_rational_rejected = true; }
    require(large_rational_rejected, "oversized snapshot rational was allowed into the GMP model state");
    auto oversized_factor_snapshot = serialized;
    const std::string world_limit = "\"max_worlds\":1048576";
    const auto world_limit_position = oversized_factor_snapshot.find(world_limit);
    require(world_limit_position != std::string::npos, "snapshot world-limit fixture not found");
    oversized_factor_snapshot.replace(world_limit_position, world_limit.size(), "\"max_worlds\":2");
    bool factor_limit_rejected = false;
    try { (void)BeliefModel::restore(oversized_factor_snapshot); }
    catch (const std::invalid_argument&) { factor_limit_rejected = true; }
    require(factor_limit_rejected, "replayed snapshot bypassed its factor-table resource limit");
}

void test_weighted_cnf_parser_to_belief_bridge() {
    std::istringstream input{
        "c t wmc\n"
        "p cnf 2 3\n"
        "c p weight 1 2/5 0\n"
        "c p weight -1 3/5 0\n"
        "c p weight 2 1/3 0\n"
        "c p weight -2 2/3 0\n"
        "1 2 0\n"
        "-1 2 0\n"
        "1 2 0\n"};
    const auto instance = xai::wmc::parse_dimacs_wmc(input);
    ModelDefinition metadata;
    metadata.run_id = RunId{"run-cnf-adapter"};
    metadata.model_id = ModelId{"model-cnf-adapter"};
    metadata.code_build_id = CodeBuildId{"git:phase2"};
    metadata.partition_id = PartitionId{"fixture-development"};
    const FormulaProvenance provenance{SourceId{"source:cnf-fixture"}, "2026-10-07T02:03:04Z",
        "sha256:weighted-cnf-fixture", "dimacs-wmc-parser-v1"};
    const auto converted = ingest_weighted_cnf(instance, std::move(metadata), provenance);
    require(converted.status == Status::success && converted.model.has_value(),
            "valid WMC input did not map to a finite factual model");
    require(converted.model->evidence().size() == 3 &&
                converted.model->evidence()[0].dependence == DependenceClass::model_factor &&
                converted.model->evidence()[2].dependence == DependenceClass::exact_duplicate &&
                converted.model->evidence()[2].dependencies.front().evidence_id == EvidenceId{"clause:1"},
            "CNF clauses or repeated-clause provenance were not retained correctly");
    const auto x1 = converted.model->query(FactId{"var:1"});
    const auto x2 = converted.model->query(FactId{"var:2"});
    require(x1.status == Status::success && x1.evidence_normalizer == q("1/3") &&
                x1.probability_true == q("2/5") && x2.status == Status::success &&
                x2.probability_true == q("1"),
            "parsed WMC weights/clauses did not preserve exact finite marginals");
    const auto replay = BeliefModel::restore(converted.model->serialize());
    require(replay.query(FactId{"var:1"}).probability_true == x1.probability_true,
            "parsed formula belief did not survive canonical replay");

    ModelDefinition limited_metadata;
    limited_metadata.run_id = RunId{"run-cnf-limited"};
    limited_metadata.model_id = ModelId{"model-cnf-limited"};
    limited_metadata.code_build_id = CodeBuildId{"git:phase2"};
    limited_metadata.partition_id = PartitionId{"fixture-development"};
    limited_metadata.limits.max_worlds = 2;
    const auto limited = ingest_weighted_cnf(instance, std::move(limited_metadata), provenance);
    require(limited.status == Status::resource_limit && !limited.model,
            "CNF state-size limit did not return an explicit resource-limit outcome");
}

void test_independent_exhaustive_oracle_grid() {
    std::mt19937 generator{0x584149U};
    for (std::uint32_t trial = 0; trial < 64; ++trial) {
        ModelDefinition definition;
        definition.run_id = RunId{"run-oracle"};
        definition.model_id = ModelId{"model-oracle"};
        definition.code_build_id = CodeBuildId{"git:phase2"};
        definition.partition_id = PartitionId{"fixture-development"};
        for (std::uint32_t i = 0; i < 4; ++i) {
            const std::string id = "fact:v" + std::to_string(i);
            definition.facts.push_back(resolved_fact(id, "formula:oracle"));
            const auto false_weight = 1U + generator() % 4U;
            const auto true_weight = 1U + generator() % 4U;
            definition.priors.push_back({FactId{id}, q(std::to_string(false_weight)), q(std::to_string(true_weight))});
        }
        auto factor = unary_evidence("evidence:oracle", {});
        factor.scope.clear();
        factor.likelihood.clear();
        for (std::uint32_t i = 0; i < 4; ++i) factor.scope.emplace_back("fact:v" + std::to_string(i));
        for (std::uint32_t state = 0; state < 16; ++state) {
            const auto numerator = generator() % 5U;
            factor.likelihood.push_back(q(std::to_string(numerator) + "/4"));
        }
        if (std::all_of(factor.likelihood.begin(), factor.likelihood.end(), [](const Rational& value) { return value == 0; }))
            factor.likelihood[trial % 16] = q("1");
        const auto factor_copy = factor;
        BeliefModel model{std::move(definition)};
        require(model.add_evidence(std::move(factor)).accepted(), "oracle-grid evidence rejected");

        Rational oracle_z0{1};
        for (const auto& prior : model.definition().priors)
            oracle_z0 *= prior.false_weight + prior.true_weight;
        Rational oracle_z{0};
        std::vector<Rational> oracle_true(4, Rational{0});
        // Separate direct oracle: evaluate every assignment and apply its table cell once.
        for (std::uint64_t state = 0; state < 16; ++state) {
            Rational mass{1};
            for (std::size_t variable = 0; variable < 4; ++variable) {
                const bool truth = ((state >> variable) & 1U) != 0;
                const auto& prior = model.definition().priors[variable];
                mass *= truth ? prior.true_weight : prior.false_weight;
            }
            mass *= factor_copy.likelihood[static_cast<std::size_t>(state)];
            oracle_z += mass;
            for (std::size_t variable = 0; variable < 4; ++variable)
                if (((state >> variable) & 1U) != 0) oracle_true[variable] += mass;
        }
        for (std::size_t variable = 0; variable < 4; ++variable) {
            const auto result = model.query(FactId{"fact:v" + std::to_string(variable)});
            require(result.status == Status::success && result.prior_normalizer == oracle_z0 &&
                        result.evidence_normalizer == oracle_z &&
                        result.probability_true == oracle_true[variable] / oracle_z,
                    "exact belief result disagrees with independent 16-world enumeration");
        }
    }
}

}  // namespace

int main() {
    try {
        test_prior_updates_and_exact_wmc_semantics();
        std::cout << "PASS exact_prior_update_and_wmc_normalizers\n";
        test_likelihood_update_and_zero_normalizers();
        std::cout << "PASS likelihood_and_zero_normalizers\n";
        test_contradictions_unknown_and_unresolved_identity();
        std::cout << "PASS contradiction_unknown_and_identity\n";
        test_duplicate_correlated_and_unresolved_dependence();
        std::cout << "PASS duplicate_correlated_and_unknown_dependence\n";
        test_validation_partition_and_resource_limits();
        std::cout << "PASS validation_partition_and_resource_limits\n";
        test_canonical_snapshot_replay_and_lineage();
        std::cout << "PASS canonical_snapshot_replay_and_lineage\n";
        test_weighted_cnf_parser_to_belief_bridge();
        std::cout << "PASS weighted_cnf_to_belief_bridge\n";
        test_independent_exhaustive_oracle_grid();
        std::cout << "PASS independent_oracle_grid (64 models x 4 marginals)\n";
        std::cout << "RESULT: 8/8 ingestion test groups passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
