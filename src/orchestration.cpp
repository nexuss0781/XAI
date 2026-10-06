#include "xai/orchestration.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(__linux__)
#include <sys/resource.h>
#endif

#ifndef XAI_BUILD_CODE_SHA256
#define XAI_BUILD_CODE_SHA256 "unconfigured-build"
#endif
#ifndef XAI_COMPILER_ID
#define XAI_COMPILER_ID "unknown"
#endif
#ifndef XAI_COMPILER_VERSION
#define XAI_COMPILER_VERSION "unknown"
#endif
#ifndef XAI_BUILD_TYPE
#define XAI_BUILD_TYPE "unknown"
#endif

namespace xai::orchestration {
namespace {
using Value = contracts::CanonicalValue;
using Object = Value::Object;
using Array = Value::Array;
using Clock = std::chrono::steady_clock;
constexpr std::string_view kInputSchemaDescriptor =
    "xai.phase8.structured-wmc-input/v1|observation_id:ObservationId|source_id:SourceId|"
    "partition_id:PartitionId|partition:DataPartition|original_input_ref:string|"
    "observed_at_utc:string|extractor_version:string|formula_text:unprojected-dimacs-wmc|"
    "uncertainty_codes:string[]|rejected_span_refs:string[]|identity_bindings:IdentityBinding[]|"
    "additional_evidence:EvidenceRecord[]|query_fact_id:optional-FactId";
constexpr std::string_view kRunSchemaDescriptor =
    "xai.phase8.run-manifest/v1|run-id|partition|module-events|input-data-hashes|"
    "code-schema-model-config-hashes|seed|limits|versions|wall-cpu-rss|semantic-sha256";
constexpr std::string_view kOutputSchemaDescriptor =
    "xai.phase8.run-output/v1|status|exactness|selected-result-or-null|decision-object-or-null|"
    "factual-query-status|input-diagnostic|run-manifest";

[[nodiscard]] std::uint64_t double_bits(double value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

class Sha256 {
public:
    void update(std::string_view input) {
        for (const unsigned char byte : input) {
            block_[block_size_++] = byte;
            bit_count_ += 8;
            if (block_size_ == block_.size()) {
                transform();
                block_size_ = 0;
            }
        }
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finish() {
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56) {
            while (block_size_ < block_.size()) block_[block_size_++] = 0;
            transform();
            block_size_ = 0;
        }
        while (block_size_ < 56) block_[block_size_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8)
            block_[block_size_++] = static_cast<std::uint8_t>((bit_count_ >> shift) & 0xffU);
        transform();
        std::array<std::uint8_t, 32> result{};
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte)
                result[word * 4 + byte] = static_cast<std::uint8_t>(
                    (state_[word] >> (24U - static_cast<unsigned int>(byte * 8))) & 0xffU);
        }
        return result;
    }

private:
    static constexpr std::array<std::uint32_t, 64> kRoundConstants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
        0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
        0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
        0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
        0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
        0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_{0};
    std::uint64_t bit_count_{0};

    static std::uint32_t rotate_right(std::uint32_t value, unsigned int bits) noexcept {
        return (value >> bits) | (value << (32U - bits));
    }

    void transform() {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto offset = index * 4;
            words[index] = (static_cast<std::uint32_t>(block_[offset]) << 24U) |
                (static_cast<std::uint32_t>(block_[offset + 1]) << 16U) |
                (static_cast<std::uint32_t>(block_[offset + 2]) << 8U) |
                static_cast<std::uint32_t>(block_[offset + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const std::uint32_t x = words[index - 15];
            const std::uint32_t y = words[index - 2];
            const std::uint32_t sigma0 = rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3U);
            const std::uint32_t sigma1 = rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10U);
            words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
        }
        auto a = state_[0]; auto b = state_[1]; auto c = state_[2]; auto d = state_[3];
        auto e = state_[4]; auto f = state_[5]; auto g = state_[6]; auto h = state_[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + sum1 + choose + kRoundConstants[index] + words[index];
            const std::uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }
};

[[nodiscard]] Value rational_value(const wmc::Rational& input) {
    auto value = input;
    value.canonicalize();
    return Object{{"$rational", Array{Value{value.get_num().get_str()},
                                       Value{value.get_den().get_str()}}}};
}

[[nodiscard]] Value optional_u64(const std::optional<std::uint64_t>& value) {
    return value ? Value{*value} : Value{nullptr};
}

[[nodiscard]] Value string_array(const std::vector<std::string>& values) {
    Array result;
    result.reserve(values.size());
    for (const auto& value : values) result.emplace_back(value);
    return result;
}

[[nodiscard]] Value limits_value(const wmc::ParseLimits& parse,
                                 const wmc::SolverOptions& solver,
                                 std::uint64_t cpu_budget,
                                 std::uint64_t memory_budget) {
    return Object{
        {"cpu_budget_ms", Value{cpu_budget}},
        {"memory_budget_bytes", Value{memory_budget}},
        {"parse_max_clauses", Value{static_cast<std::uint64_t>(parse.max_clauses)}},
        {"parse_max_input_bytes", Value{parse.max_input_bytes}},
        {"parse_max_line_bytes", Value{static_cast<std::uint64_t>(parse.max_line_bytes)}},
        {"parse_max_literals", Value{parse.max_literals}},
        {"parse_max_variables", Value{static_cast<std::uint64_t>(parse.max_variables)}},
        {"solver_component_decomposition", Value{solver.component_decomposition}},
        {"solver_max_depth", Value{static_cast<std::uint64_t>(solver.max_depth)}},
        {"solver_node_limit", Value{solver.node_limit}},
        {"solver_occurrence_branching", Value{solver.occurrence_branching}},
        {"solver_timeout_ms", Value{solver.timeout_ms}},
        {"solver_unit_propagation", Value{solver.unit_propagation}}};
}

[[nodiscard]] Value instance_value(const wmc::Instance& instance) {
    Array clauses;
    clauses.reserve(instance.clauses.size());
    for (const auto& clause : instance.clauses) {
        Array literals;
        literals.reserve(clause.size());
        for (const auto literal : clause) literals.emplace_back(static_cast<std::int64_t>(literal));
        clauses.emplace_back(std::move(literals));
    }
    Array weights;
    weights.reserve(instance.weights.size());
    for (const auto& pair : instance.weights)
        weights.emplace_back(Object{{"negative", rational_value(pair.negative)},
                                    {"positive", rational_value(pair.positive)}});
    return Object{{"clauses", Value{std::move(clauses)}},
        {"declared_clauses", Value{static_cast<std::uint64_t>(instance.declared_clauses)}},
        {"variables", Value{static_cast<std::uint64_t>(instance.variables)}},
        {"weights", Value{std::move(weights)}}};
}

[[nodiscard]] std::string config_sha256(const RunOptions& options,
                                        const decision::CertificateVerifier* verifier) {
    Object config{{"build_type", Value{XAI_BUILD_TYPE}},
        {"code_build_id", Value{options.code_build_id.value()}},
        {"decision_abstention_cost", rational_value(options.decision_policy.abstention_cost)},
        {"decision_max_unresolved", rational_value(options.decision_policy.maximum_unresolved_probability)},
        {"decision_resource_cost_weight", rational_value(options.decision_policy.resource_cost_weight)},
        {"decision_time_epoch", Value{options.decision_policy.decision_time_epoch}},
        {"decision_verifier_id", Value{options.decision_policy.verifier_id}},
        {"decision_verifier_version", Value{options.decision_policy.verifier_version}},
        {"effective_verifier_id", Value{verifier ? std::string(verifier->id()) : options.decision_policy.verifier_id}},
        {"effective_verifier_version", Value{verifier ? std::string(verifier->version()) : options.decision_policy.verifier_version}},
        {"decision_require_exact", Value{options.decision_policy.require_exact_output}},
        {"limits", limits_value(options.parse_limits, options.solver_options,
                                  options.cpu_budget_ms, options.memory_budget_bytes)},
        {"seed", optional_u64(options.seed)}};
    if (options.adaptation) {
        const auto& scenario = *options.adaptation;
        config.emplace("adaptation_suite_id", Value{scenario.development_suite.suite_id});
        config.emplace("adaptation_partition", Value{std::string(
            contracts::to_string(scenario.development_suite.partition))});
        config.emplace("adaptation_stream_observation", Value{
            scenario.streaming_observation.observation_id.value()});
        config.emplace("adaptation_stream_partition", Value{std::string(
            contracts::to_string(scenario.streaming_observation.partition))});
        config.emplace("adaptation_stream_nodes", Value{scenario.streaming_observation.recursive_nodes});
        config.emplace("adaptation_stream_solver_success", Value{scenario.streaming_observation.solver_success});
        config.emplace("adaptation_partition_id", Value{scenario.development_suite.partition_id.value()});
        config.emplace("adaptation_baseline_version", Value{scenario.controller_options.baseline_version});
        config.emplace("adaptation_mu_in_control_f64_bits", Value{double_bits(scenario.controller_options.detector.mu_in_control)});
        config.emplace("adaptation_mu_shift_f64_bits", Value{double_bits(scenario.controller_options.detector.mu_shift)});
        config.emplace("adaptation_sigma_f64_bits", Value{double_bits(scenario.controller_options.detector.sigma)});
        config.emplace("adaptation_threshold_f64_bits", Value{double_bits(scenario.controller_options.detector.threshold)});
        config.emplace("adaptation_cadence", Value{scenario.controller_options.detector.sample_cadence});
        config.emplace("adaptation_hold_off", Value{scenario.controller_options.detector.hold_off_samples});
        Array cases;
        for (const auto& item : scenario.development_suite.cases)
            cases.emplace_back(Object{{"expected_count", rational_value(item.expected_count)},
                {"expected_satisfiable", Value{item.expected_satisfiable}},
                {"instance", instance_value(item.instance)},
                {"observation_id", Value{item.observation_id.value()}},
                {"partition", Value{std::string(contracts::to_string(item.partition))}}});
        config.emplace("adaptation_development_cases", Value{std::move(cases)});
    } else {
        config.emplace("adaptation", Value{nullptr});
    }
    if (options.certificate) {
        config.emplace("certificate_id", Value{options.certificate->certificate_id.value()});
        config.emplace("certificate_action_id", Value{options.certificate->action_id});
        config.emplace("certificate_property_id", Value{options.certificate->property_id});
        config.emplace("certificate_subject_sha256", Value{options.certificate->subject_sha256});
        config.emplace("certificate_claim_sha256", Value{options.certificate->claim_sha256});
        config.emplace("certificate_verifier_id", Value{options.certificate->verifier_id});
        config.emplace("certificate_verifier_version", Value{options.certificate->verifier_version});
        config.emplace("certificate_valid_from_epoch", Value{options.certificate->valid_from_epoch});
        config.emplace("certificate_expires_at_epoch", Value{options.certificate->expires_at_epoch});
    } else {
        config.emplace("certificate", Value{nullptr});
    }
    return sha256_hex(contracts::encode_canonical_value(Value{std::move(config)}));
}

[[nodiscard]] std::string status_name(contracts::Status status) {
    return std::string(contracts::to_string(status));
}

[[nodiscard]] ModuleEvent event(std::string module, ModuleDisposition disposition,
                                contracts::Status status, std::string reason) {
    return {std::move(module), disposition, status, std::move(reason)};
}

void append_event(RunManifest& manifest, std::string module,
                  ModuleDisposition disposition, contracts::Status status,
                  std::string reason) {
    manifest.modules.push_back(event(std::move(module), disposition, status, std::move(reason)));
}

[[nodiscard]] bool valid_text(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (const unsigned char ch : value)
        if (ch < 0x20U || ch == 0x7fU) return false;
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> peak_rss_bytes() {
#if defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) return std::nullopt;
    const auto kib = static_cast<std::uint64_t>(usage.ru_maxrss);
    if (kib > std::numeric_limits<std::uint64_t>::max() / 1024U) return std::nullopt;
    return kib * 1024U;
#else
    return std::nullopt;
#endif
}

[[nodiscard]] std::optional<std::uint64_t> process_cpu_ms() {
#if defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return std::nullopt;
    const auto user_ms = static_cast<std::uint64_t>(usage.ru_utime.tv_sec) * 1000U +
        static_cast<std::uint64_t>(usage.ru_utime.tv_usec) / 1000U;
    const auto system_ms = static_cast<std::uint64_t>(usage.ru_stime.tv_sec) * 1000U +
        static_cast<std::uint64_t>(usage.ru_stime.tv_usec) / 1000U;
    if (user_ms > std::numeric_limits<std::uint64_t>::max() - system_ms) return std::nullopt;
    return user_ms + system_ms;
#else
    return std::nullopt;
#endif
}

[[nodiscard]] std::string_view adaptation_event_name(adaptation::EventKind kind) noexcept {
    switch (kind) {
    case adaptation::EventKind::search_updated: return "search_updated";
    case adaptation::EventKind::search_unchanged: return "search_unchanged";
    case adaptation::EventKind::partition_rejected: return "partition_rejected";
    case adaptation::EventKind::residual_observed: return "residual_observed";
    case adaptation::EventKind::regression_rollback: return "regression_rollback";
    case adaptation::EventKind::invariant_rollback: return "invariant_rollback";
    case adaptation::EventKind::shift_alarm_rollback: return "shift_alarm_rollback";
    case adaptation::EventKind::reset: return "reset";
    }
    return "invalid_event";
}

[[nodiscard]] Value identity_bindings_value(const std::vector<IdentityBinding>& bindings);

[[nodiscard]] std::string semantic_digest(const RunResult& result) {
    Object root{{"exactness", Value{std::string(contracts::to_string(result.exactness))}},
        {"input_diagnostic", Value{result.input_diagnostic}},
        {"model_sha256", result.factual_model_sha256 ? Value{*result.factual_model_sha256} : Value{nullptr}},
        {"query_status", result.factual_query_status ?
            Value{std::string(contracts::to_string(*result.factual_query_status))} : Value{nullptr}},
        {"run_id", Value{result.manifest.run_id.value()}},
        {"status", Value{status_name(result.status)}},
        {"data_sha256", Value{result.manifest.data_sha256}},
        {"supplemental_input_sha256", Value{result.manifest.supplemental_input_sha256}},
        {"config_sha256", Value{result.manifest.config_sha256}}};
    root.emplace("satisfiable", result.satisfiable ? Value{*result.satisfiable} : Value{nullptr});
    root.emplace("exact_count", result.exact_count ? rational_value(*result.exact_count) : Value{nullptr});
    Array modules;
    for (const auto& item : result.manifest.modules)
        modules.emplace_back(Object{{"disposition", Value{std::string(to_string(item.disposition))}},
            {"module", Value{item.module}}, {"reason", Value{item.reason}},
            {"status", Value{status_name(item.status)}}});
    root.emplace("modules", Value{std::move(modules)});
    root.emplace("identity_bindings", identity_bindings_value(result.manifest.identity_bindings));
    root.emplace("uncertainty_codes", string_array(result.manifest.uncertainty_codes));
    root.emplace("rejected_span_refs", string_array(result.manifest.rejected_span_refs));
    Array adaptation_events;
    for (const auto& audit : result.manifest.adaptation_audit)
        adaptation_events.emplace_back(Object{{"detail", Value{audit.detail}},
            {"cusum_f64_bits", audit.cusum ? Value{double_bits(*audit.cusum)} : Value{nullptr}},
            {"kind", Value{std::string(adaptation_event_name(audit.kind))}},
            {"residual_f64_bits", audit.residual ? Value{double_bits(*audit.residual)} : Value{nullptr}},
            {"sequence", Value{audit.sequence}},
            {"suite_or_observation_id", Value{audit.suite_or_observation_id}}});
    root.emplace("adaptation_audit", Value{std::move(adaptation_events)});
    if (result.decision_output) {
        root.emplace("decision_status", Value{std::string(decision::to_string(result.decision_output->status))});
        root.emplace("decision_upstream_status", Value{status_name(result.decision_output->upstream_status)});
        root.emplace("decision_action_id", result.decision_output->action_id ?
            Value{*result.decision_output->action_id} : Value{nullptr});
        root.emplace("decision_action_value", result.decision_output->action_value ?
            *result.decision_output->action_value : Value{nullptr});
        Array assessments;
        for (const auto& assessment : result.decision_output->assessments)
            assessments.emplace_back(Object{{"action_id", Value{assessment.action_id}},
                {"admissible", Value{assessment.admissible}},
                {"certificate_status", Value{std::string(decision::to_string(assessment.certificate_status))}}});
        root.emplace("decision_assessments", Value{std::move(assessments)});
    }
    return sha256_hex(contracts::encode_canonical_value(Value{std::move(root)}));
}

[[nodiscard]] Value identity_bindings_value(const std::vector<IdentityBinding>& bindings) {
    Array values;
    values.reserve(bindings.size());
    for (const auto& binding : bindings) {
        Array candidates;
        for (const auto& candidate : binding.identity.candidate_entity_ids)
            candidates.emplace_back(candidate.value());
        values.emplace_back(Object{{"candidate_entity_ids", Value{std::move(candidates)}},
            {"entity_id", binding.identity.entity_id ? Value{binding.identity.entity_id->value()} : Value{nullptr}},
            {"required_for_task", Value{binding.required_for_task}},
            {"resolution", Value{std::string(ingestion::to_string(binding.identity.resolution))}},
            {"variable_index", Value{static_cast<std::uint64_t>(binding.variable_index)}}});
    }
    return values;
}

[[nodiscard]] Value evidence_records_value(
    const std::vector<ingestion::EvidenceRecord>& records) {
    Array values;
    values.reserve(records.size());
    for (const auto& evidence : records) {
        Array scope;
        for (const auto& fact : evidence.scope) scope.emplace_back(fact.value());
        Array likelihood;
        for (const auto& potential : evidence.likelihood) likelihood.emplace_back(rational_value(potential));
        Array dependencies;
        for (const auto& dependency : evidence.dependencies)
            dependencies.emplace_back(Object{{"evidence_id", Value{dependency.evidence_id.value()}},
                {"relation", Value{std::string(ingestion::to_string(dependency.relation))}}});
        values.emplace_back(Object{{"dependence", Value{std::string(ingestion::to_string(evidence.dependence))}},
            {"dependencies", Value{std::move(dependencies)}},
            {"evidence_id", Value{evidence.evidence_id.value()}},
            {"extractor_version", Value{evidence.extractor_version}},
            {"likelihood", Value{std::move(likelihood)}},
            {"observed_at_utc", Value{evidence.observed_at_utc}},
            {"original_observation_ref", Value{evidence.original_observation_ref}},
            {"partition", Value{std::string(contracts::to_string(evidence.partition))}},
            {"scope", Value{std::move(scope)}},
            {"semantics", Value{std::string(ingestion::to_string(evidence.semantics))}},
            {"source_id", Value{evidence.source_id.value()}}});
    }
    return values;
}

[[nodiscard]] Value supplemental_input_value(const StructuredWmcInput& input) {
    return Object{{"additional_evidence", evidence_records_value(input.additional_evidence)},
        {"identity_bindings", identity_bindings_value(input.identity_bindings)},
        {"query_fact_id", input.query_fact_id ? Value{input.query_fact_id->value()} : Value{nullptr}},
        {"rejected_span_refs", string_array(input.rejected_span_refs)},
        {"uncertainty_codes", string_array(input.uncertainty_codes)}};
}

[[nodiscard]] Value module_events_value(const std::vector<ModuleEvent>& modules) {
    Array result;
    result.reserve(modules.size());
    for (const auto& item : modules)
        result.emplace_back(Object{{"disposition", Value{std::string(to_string(item.disposition))}},
            {"module", Value{item.module}}, {"reason", Value{item.reason}},
            {"status", Value{status_name(item.status)}}});
    return result;
}

[[nodiscard]] Value manifest_value(const RunManifest& manifest) {
    Object resources{{"elapsed_wall_ms", Value{manifest.resources.elapsed_wall_ms}},
        {"process_cpu_ms", manifest.resources.process_cpu_ms ?
            Value{*manifest.resources.process_cpu_ms} : Value{nullptr}},
        {"peak_rss_bytes", manifest.resources.process_peak_rss_bytes ?
            Value{*manifest.resources.process_peak_rss_bytes} : Value{nullptr}},
        {"solver_recursive_nodes", Value{manifest.resources.solver_recursive_nodes}}};
    Array adaptation_audit;
    for (const auto& audit : manifest.adaptation_audit) {
        Array scores;
        for (const auto& score : audit.candidate_scores)
            scores.emplace_back(Object{{"completed", Value{static_cast<std::uint64_t>(score.completed)}},
                {"mean_log_nodes_f64_bits", Value{double_bits(score.mean_log_nodes)}},
                {"node_limit", Value{score.node_limit}},
                {"total_cases", Value{static_cast<std::uint64_t>(score.total_cases)}},
                {"total_recursive_nodes", Value{score.total_recursive_nodes}}});
        adaptation_audit.emplace_back(Object{
            {"baseline_completed", audit.baseline_completed ? Value{static_cast<std::uint64_t>(*audit.baseline_completed)} : Value{nullptr}},
            {"candidate_scores", Value{std::move(scores)}},
            {"cusum_f64_bits", audit.cusum ? Value{double_bits(*audit.cusum)} : Value{nullptr}},
            {"detail", Value{audit.detail}},
            {"from_version", Value{audit.from_version}},
            {"kind", Value{std::string(adaptation_event_name(audit.kind))}},
            {"partition", Value{std::string(contracts::to_string(audit.partition))}},
            {"residual_f64_bits", audit.residual ? Value{double_bits(*audit.residual)} : Value{nullptr}},
            {"selected_completed", audit.selected_completed ? Value{static_cast<std::uint64_t>(*audit.selected_completed)} : Value{nullptr}},
            {"sequence", Value{audit.sequence}},
            {"suite_or_observation_id", Value{audit.suite_or_observation_id}},
            {"to_version", Value{audit.to_version}}});
    }
    return Object{
        {"adaptation_audit", Value{std::move(adaptation_audit)}},
        {"build_type", Value{manifest.build_type}},
        {"code_build_id", Value{manifest.code_build_id}},
        {"code_sha256", Value{manifest.code_sha256}},
        {"compiler_id", Value{manifest.compiler_id}},
        {"compiler_version", Value{manifest.compiler_version}},
        {"config_sha256", Value{manifest.config_sha256}},
        {"data_sha256", Value{manifest.data_sha256}},
        {"supplemental_input_sha256", Value{manifest.supplemental_input_sha256}},
        {"input_bytes", Value{manifest.input_bytes}},
        {"input_schema_sha256", Value{manifest.input_schema_sha256}},
        {"output_schema_sha256", Value{manifest.output_schema_sha256}},
        {"identity_bindings", identity_bindings_value(manifest.identity_bindings)},
        {"uncertainty_codes", Value{string_array(manifest.uncertainty_codes)}},
        {"rejected_span_refs", Value{string_array(manifest.rejected_span_refs)}},
        {"memory_budget_bytes", Value{manifest.memory_budget_bytes}},
        {"model_sha256", Value{manifest.model_sha256}},
        {"modules", module_events_value(manifest.modules)},
        {"observation_id", Value{manifest.observation_id.value()}},
        {"original_input_ref", Value{manifest.original_input_ref}},
        {"parser_version", Value{manifest.parser_version}},
        {"extractor_version", Value{manifest.extractor_version}},
        {"partition", Value{std::string(contracts::to_string(manifest.partition))}},
        {"partition_id", Value{manifest.partition_id.value()}},
        {"run_id", Value{manifest.run_id.value()}},
        {"run_schema_sha256", Value{manifest.run_schema_sha256}},
        {"schema_version", Value{kSchemaVersion}},
        {"seed", optional_u64(manifest.seed)},
        {"solver_limits", limits_value(manifest.parse_limits, manifest.solver_limits,
                                         manifest.cpu_budget_ms, manifest.memory_budget_bytes)},
        {"solver_version", Value{manifest.solver_version}},
        {"verifier_id", Value{manifest.verifier_id}},
        {"verifier_version", Value{manifest.verifier_version}},
        {"source_id", Value{manifest.source_id.value()}},
        {"resources", Value{std::move(resources)}},
        {"cpu_budget_ms", Value{manifest.cpu_budget_ms}}};
}

[[nodiscard]] Value optional_decision_rational(const std::optional<decision::Rational>& value) {
    return value ? rational_value(*value) : Value{nullptr};
}

[[nodiscard]] Value decision_value(const decision::DecisionOutput& output) {
    Array assessments;
    for (const auto& assessment : output.assessments)
        assessments.emplace_back(Object{{"action_id", Value{assessment.action_id}},
            {"admissible", Value{assessment.admissible}},
            {"certificate_status", Value{std::string(decision::to_string(assessment.certificate_status))}},
            {"diagnostic", Value{assessment.diagnostic}},
            {"expected_risk", optional_decision_rational(assessment.expected_risk)},
            {"resource_cost", optional_decision_rational(assessment.resource_cost)}});
    return Object{
        {"action_assessments", Value{std::move(assessments)}},
        {"action_id", output.action_id ? Value{*output.action_id} : Value{nullptr}},
        {"action_label", output.action_label ? Value{*output.action_label} : Value{nullptr}},
        {"action_value", output.action_value ? *output.action_value : Value{nullptr}},
        {"assumptions", string_array(output.assumptions)},
        {"claim_sha256", output.claim_sha256 ? Value{*output.claim_sha256} : Value{nullptr}},
        {"decision_schema_id", Value{"xai.decision.output"}},
        {"decision_schema_version", Value{1}},
        {"diagnostic", Value{output.diagnostic}},
        {"exactness", Value{std::string(contracts::to_string(output.exactness))}},
        {"expected_risk", optional_decision_rational(output.expected_risk)},
        {"limitations", string_array(output.limitations)},
        {"lineage", string_array(output.lineage)},
        {"resource_cost", optional_decision_rational(output.resource_cost)},
        {"status", Value{std::string(decision::to_string(output.status))}},
        {"unresolved_probability", optional_decision_rational(output.unresolved_probability)},
        {"upstream_status", Value{status_name(output.upstream_status)}}};
}

[[nodiscard]] bool valid_hash(std::string_view digest) {
    if (digest.size() != 64) return false;
    return std::all_of(digest.begin(), digest.end(), [](unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

[[nodiscard]] bool evidence_requires_model(const StructuredWmcInput& input) {
    return !input.additional_evidence.empty() || input.query_fact_id.has_value();
}

}  // namespace

std::string sha256_hex(std::string_view bytes) {
    Sha256 hash;
    hash.update(bytes);
    const auto digest = hash.finish();
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : digest) output << std::setw(2) << static_cast<unsigned int>(byte);
    return output.str();
}

std::string_view to_string(ModuleDisposition value) noexcept {
    switch (value) {
    case ModuleDisposition::executed: return "executed";
    case ModuleDisposition::skipped: return "skipped";
    case ModuleDisposition::failed: return "failed";
    }
    return "invalid_disposition";
}

[[nodiscard]] RunResult execute_internal(const StructuredWmcInput& input, const RunOptions& options,
                                         const decision::CertificateVerifier* verifier,
                                         Clock::time_point started,
                                         std::optional<std::uint64_t> cpu_started) {
    RunResult result;
    auto& manifest = result.manifest;
    manifest.run_id = options.run_id;
    manifest.observation_id = input.observation_id;
    manifest.source_id = input.source_id;
    manifest.partition_id = input.partition_id;
    manifest.partition = input.partition;
    manifest.original_input_ref = input.original_input_ref;
    manifest.code_build_id = options.code_build_id.value();
    manifest.code_sha256 = XAI_BUILD_CODE_SHA256;
    manifest.compiler_id = XAI_COMPILER_ID;
    manifest.compiler_version = XAI_COMPILER_VERSION;
    manifest.build_type = XAI_BUILD_TYPE;
    manifest.verifier_id = verifier ? std::string(verifier->id()) : options.decision_policy.verifier_id;
    manifest.verifier_version = verifier ? std::string(verifier->version()) : options.decision_policy.verifier_version;
    manifest.parser_version = std::string(kParserVersion);
    manifest.extractor_version = input.extractor_version;
    manifest.solver_version = std::string(kSolverVersion);
    manifest.seed = options.seed;
    manifest.parse_limits = options.parse_limits;
    manifest.solver_limits = options.solver_options;
    manifest.cpu_budget_ms = options.cpu_budget_ms;
    manifest.memory_budget_bytes = options.memory_budget_bytes;
    manifest.config_sha256 = config_sha256(options, verifier);
    manifest.input_schema_sha256 = sha256_hex(kInputSchemaDescriptor);
    manifest.run_schema_sha256 = sha256_hex(kRunSchemaDescriptor);
    manifest.output_schema_sha256 = sha256_hex(kOutputSchemaDescriptor);
    const auto fail_before_data_access = [&](contracts::Status status, std::string message,
                                             std::string reason) {
        result.status = status;
        result.input_diagnostic = std::move(message);
        append_event(manifest, "input_mapping", ModuleDisposition::failed, status, std::move(reason));
        manifest.resources.elapsed_wall_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
        result.semantic_sha256 = semantic_digest(result);
        return result;
    };

    // Partition and identifier checks deliberately precede any hash or parse of formula_text.
    if (input.partition != contracts::DataPartition::development ||
        input.partition_id.value() != kPartitionId) {
        return fail_before_data_access(contracts::Status::unsupported_input,
            "Only caller-labeled public-even development inputs are supported; final-test and other partitions are rejected.",
            "partition boundary rejected before formula bytes were accessed");
    }
    manifest.identity_bindings = input.identity_bindings;
    manifest.uncertainty_codes = input.uncertainty_codes;
    manifest.rejected_span_refs = input.rejected_span_refs;
    if (!input.observation_id.valid() || !input.source_id.valid() || !input.partition_id.valid() ||
        !options.run_id.valid() || !options.code_build_id.valid() ||
        !valid_text(input.original_input_ref, 1024) || !valid_text(input.observed_at_utc, 20) ||
        !valid_text(input.extractor_version, 128) || options.cpu_budget_ms == 0 ||
        options.cpu_budget_ms > 600'000 || options.memory_budget_bytes == 0 ||
        options.memory_budget_bytes > 4ULL * 1024ULL * 1024ULL * 1024ULL ||
        options.parse_limits.max_input_bytes == 0 ||
        options.parse_limits.max_input_bytes > 4ULL * 1024ULL * 1024ULL * 1024ULL ||
        options.solver_options.timeout_ms > 600'000 ||
        !valid_hash(manifest.code_sha256)) {
        return fail_before_data_access(contracts::Status::invalid_schema,
            "Run identifiers, source metadata, build fingerprint, or declared limits are invalid.",
            "structured input or run configuration failed schema validation");
    }
    const auto valid_annotations = [](const std::vector<std::string>& values) {
        return values.size() <= 1024 && std::all_of(values.begin(), values.end(),
            [](const auto& value) { return contracts::valid_identifier(value); });
    };
    if (!valid_annotations(input.uncertainty_codes) || !valid_annotations(input.rejected_span_refs)) {
        return fail_before_data_access(contracts::Status::invalid_schema,
            "Uncertainty codes and rejected-span references must be bounded stable identifiers.",
            "input annotation schema validation failed");
    }
    if (input.identity_bindings.size() > 10'000 || input.additional_evidence.size() > 10'000) {
        return fail_before_data_access(contracts::Status::resource_limit,
            "Identity or evidence records exceed the supplemental-input harness cap.",
            "supplemental record-count limit reached");
    }
    std::size_t candidate_count = 0;
    for (const auto& binding : input.identity_bindings) {
        const auto& candidates = binding.identity.candidate_entity_ids;
        if (candidates.size() > 1024 || candidates.size() > 100'000 - candidate_count) {
            return fail_before_data_access(contracts::Status::resource_limit,
                "Identity candidates exceed the supplemental-input harness cap.",
                "identity-candidate limit reached");
        }
        candidate_count += candidates.size();
        if ((binding.identity.entity_id && !binding.identity.entity_id->valid()) ||
            !std::all_of(candidates.begin(), candidates.end(), [](const auto& candidate) { return candidate.valid(); })) {
            return fail_before_data_access(contracts::Status::invalid_schema,
                "Identity bindings contain an invalid entity identifier.",
                "identity identifier validation failed");
        }
    }
    std::uint64_t evidence_entries = 0;
    std::size_t dependency_count = 0;
    for (const auto& evidence : input.additional_evidence) {
        if (evidence.scope.size() > 20 || evidence.dependencies.size() > 10'000 ||
            evidence.dependencies.size() > 100'000 - dependency_count ||
            evidence.likelihood.size() > 1'000'000 ||
            evidence.likelihood.size() > 1'000'000 - evidence_entries) {
            return fail_before_data_access(contracts::Status::resource_limit,
                "Supplemental evidence exceeds the bounded scope, factor, or dependency budget.",
                "supplemental evidence factor/dependency limit reached");
        }
        dependency_count += evidence.dependencies.size();
        evidence_entries += static_cast<std::uint64_t>(evidence.likelihood.size());
        if (!evidence.evidence_id.valid() || !evidence.source_id.valid() ||
            !valid_text(evidence.original_observation_ref, 1024) ||
            !valid_text(evidence.observed_at_utc, 20) || !valid_text(evidence.extractor_version, 128) ||
            !std::all_of(evidence.scope.begin(), evidence.scope.end(), [](const auto& fact) { return fact.valid(); }) ||
            !std::all_of(evidence.dependencies.begin(), evidence.dependencies.end(),
                [](const auto& dependency) { return dependency.evidence_id.valid(); })) {
            return fail_before_data_access(contracts::Status::invalid_schema,
                "Supplemental evidence contains invalid identifiers or provenance references.",
                "evidence provenance validation failed");
        }
    }
    try {
        manifest.supplemental_input_sha256 = sha256_hex(
            contracts::encode_canonical_value(supplemental_input_value(input)));
    } catch (const std::exception&) {
        return fail_before_data_access(contracts::Status::invalid_schema,
            "Supplemental evidence or annotations cannot be canonically represented.",
            "supplemental input failed canonical serialization");
    }
    if (input.formula_text.size() > options.parse_limits.max_input_bytes) {
        return fail_before_data_access(contracts::Status::resource_limit,
            "Structured formula exceeds the configured input-byte limit.",
            "formula size exceeds parser budget before parsing");
    }
    manifest.input_bytes = static_cast<std::uint64_t>(input.formula_text.size());
    manifest.data_sha256 = sha256_hex(input.formula_text);
    append_event(manifest, "input_mapping", ModuleDisposition::executed, contracts::Status::success,
        "Structured DIMACS-WMC input accepted; original reference, parser version, partition, and content hash recorded.");
    if (!input.rejected_span_refs.empty()) {
        result.status = contracts::Status::unsupported_input;
        result.input_diagnostic = "Input declares rejected or unsupported content spans; partial interpretation is not allowed.";
        append_event(manifest, "input_mapping", ModuleDisposition::failed, result.status,
            "Caller-declared unsupported spans were retained by reference and prevented partial task mapping.");
        result.semantic_sha256 = semantic_digest(result);
        return result;
    }

    wmc::Instance instance;
    try {
        std::istringstream formula(input.formula_text);
        instance = wmc::parse_dimacs_wmc(formula, options.parse_limits);
    } catch (const wmc::UnsupportedInput&) {
        result.status = contracts::Status::unsupported_input;
        result.input_diagnostic = "Unsupported DIMACS-WMC syntax; parser token details are excluded from the run report.";
        append_event(manifest, "input_mapping", ModuleDisposition::failed, result.status,
                     "The deterministic parser rejected an unsupported WMC feature; no coercion was attempted.");
        result.semantic_sha256 = semantic_digest(result);
        return result;
    } catch (const wmc::ResourceLimit&) {
        result.status = contracts::Status::resource_limit;
        result.input_diagnostic = "DIMACS-WMC parser resource limit reached; formula text is excluded from diagnostics.";
        append_event(manifest, "input_mapping", ModuleDisposition::failed, result.status,
                     "Parser resource limit reached without producing a partial instance.");
        result.semantic_sha256 = semantic_digest(result);
        return result;
    } catch (const wmc::ParseError&) {
        result.status = contracts::Status::invalid_schema;
        result.input_diagnostic = "Malformed DIMACS-WMC structure; parser token details are excluded from the run report.";
        append_event(manifest, "input_mapping", ModuleDisposition::failed, result.status,
                     "Malformed DIMACS-WMC structure rejected without repair or coercion.");
        result.semantic_sha256 = semantic_digest(result);
        return result;
    }

    const auto typed_model = instance_value(instance);
    manifest.model_sha256 = sha256_hex(contracts::encode_canonical_value(typed_model));
    std::map<std::uint32_t, bool> bound_variables;
    for (const auto& binding : input.identity_bindings) {
        if (binding.variable_index == 0 || binding.variable_index > instance.variables ||
            !bound_variables.emplace(binding.variable_index, true).second) {
            result.status = contracts::Status::invalid_schema;
            result.input_diagnostic = "Identity binding has a duplicate or out-of-range formal variable index.";
            append_event(manifest, "input_mapping", ModuleDisposition::failed, result.status,
                         "Identity-binding schema validation failed; candidate annotations were not collapsed.");
            result.semantic_sha256 = semantic_digest(result);
            return result;
        }
        const auto& identity = binding.identity;
        if (identity.resolution == ingestion::IdentityResolution::resolved &&
            (!identity.entity_id || !identity.entity_id->valid() || !identity.candidate_entity_ids.empty())) {
            result.status = contracts::Status::invalid_schema;
            result.input_diagnostic = "Resolved identity binding must contain exactly one valid entity ID.";
            append_event(manifest, "input_mapping", ModuleDisposition::failed, result.status,
                         "Identity-binding schema validation failed.");
            result.semantic_sha256 = semantic_digest(result);
            return result;
        }
        if (identity.resolution == ingestion::IdentityResolution::unresolved &&
            (identity.entity_id || std::any_of(identity.candidate_entity_ids.begin(),
                identity.candidate_entity_ids.end(), [](const auto& id) { return !id.valid(); }))) {
            result.status = contracts::Status::invalid_schema;
            result.input_diagnostic = "Unresolved identity binding cannot contain a resolved entity ID or invalid candidate.";
            append_event(manifest, "input_mapping", ModuleDisposition::failed, result.status,
                         "Identity-binding schema validation failed.");
            result.semantic_sha256 = semantic_digest(result);
            return result;
        }
        if (binding.required_for_task && identity.resolution == ingestion::IdentityResolution::unresolved) {
            result.status = contracts::Status::unsupported_input;
            result.input_diagnostic = "Required entity identity is unresolved; the runtime will not choose among candidates.";
            append_event(manifest, "input_mapping", ModuleDisposition::failed, result.status,
                         "Unresolved identity candidates were retained and the dependent task was rejected.");
            result.semantic_sha256 = semantic_digest(result);
            return result;
        }
    }

    append_event(manifest, "predictive_learning", ModuleDisposition::skipped, contracts::Status::unknown,
        "No outcome labels or authorized training update are required for deterministic exact WMC.");
    append_event(manifest, "finite_reasoning", ModuleDisposition::skipped, contracts::Status::unknown,
        "The bounded generic finite-domain enumerator is not needed; the typed CNF is handled by the exact WMC solver.");
    append_event(manifest, "calibration", ModuleDisposition::skipped, contracts::Status::unknown,
        "The Phase 0 archive has no labels or authorized calibration partition; no calibration claim is emitted.");

    contracts::Status upstream_status = contracts::Status::success;
    std::uint64_t solver_node_limit = options.solver_options.node_limit;
    if (options.adaptation) {
        try {
            adaptation::Controller controller(options.adaptation->controller_options);
            const auto search = controller.search(options.adaptation->development_suite);
            manifest.adaptation_audit = controller.audit_log();
            if (search.decision == adaptation::Decision::rejected_partition ||
                search.decision == adaptation::Decision::invalid_input) {
                result.status = search.decision == adaptation::Decision::rejected_partition ?
                    contracts::Status::unsupported_input : contracts::Status::invalid_schema;
                result.input_diagnostic = "Adaptation development suite was rejected before candidate scoring: " + search.diagnostic;
                append_event(manifest, "adaptation", ModuleDisposition::failed, result.status,
                             "Partition/schema boundary prevented development-only adaptation.");
                result.semantic_sha256 = semantic_digest(result);
                return result;
            }
            solver_node_limit = search.active_node_limit;
            const auto monitored = controller.observe(options.adaptation->streaming_observation);
            manifest.adaptation_audit = controller.audit_log();
            solver_node_limit = monitored.active_node_limit;
            if (monitored.decision == adaptation::Decision::rejected_partition) {
                result.status = contracts::Status::unsupported_input;
                result.input_diagnostic = "Adaptation monitor rejected a non-streaming partition.";
                append_event(manifest, "adaptation", ModuleDisposition::failed, result.status,
                             "Partition boundary rejected the observation before residual evaluation.");
                result.semantic_sha256 = semantic_digest(result);
                return result;
            }
            if (monitored.decision == adaptation::Decision::alarm_frozen) {
                upstream_status = contracts::Status::alarm_frozen;
                result.input_diagnostic = monitored.diagnostic;
            }
            append_event(manifest, "adaptation", ModuleDisposition::executed,
                upstream_status, "Development search and streaming monitor executed; active_node_limit=" +
                std::to_string(monitored.active_node_limit) + "; state=" +
                (monitored.state == adaptation::ControllerState::frozen ? "frozen" : "exploring") +
                "; audit_events=" + std::to_string(controller.audit_log().size()) +
                (monitored.residual ? "; residual=" + std::to_string(*monitored.residual) : "") +
                "; cusum=" + std::to_string(monitored.cusum));
        } catch (const std::exception& error) {
            result.status = contracts::Status::invalid_schema;
            result.input_diagnostic = std::string("Adaptation stage rejected its configuration: ") + error.what();
            append_event(manifest, "adaptation", ModuleDisposition::failed, result.status,
                         "Adaptation stage failed validation; no candidate state is trusted.");
            result.semantic_sha256 = semantic_digest(result);
            return result;
        }
    } else {
        append_event(manifest, "adaptation", ModuleDisposition::skipped, contracts::Status::unknown,
            "No labeled development objective and streaming monitor were supplied; the versioned baseline remains active.");
    }
    manifest.solver_limits.node_limit = solver_node_limit;

    const bool ingestion_fits = instance.variables <= 20 &&
        instance.variables <= 63 && instance.clauses.size() <= 10'000;
    if (!ingestion_fits) {
        if (evidence_requires_model(input)) {
            result.status = contracts::Status::resource_limit;
            result.input_diagnostic = "Requested factual query/evidence exceeds the bounded finite-ingestion state cap.";
            append_event(manifest, "factual_ingestion", ModuleDisposition::failed, result.status,
                         "Required evidence model cannot be constructed within its declared world/evidence budget.");
            result.semantic_sha256 = semantic_digest(result);
            return result;
        }
        append_event(manifest, "factual_ingestion", ModuleDisposition::skipped, contracts::Status::resource_limit,
            "Formula exceeds the finite-ingestion harness cap; raw exact WMC remains independently supported.");
    } else {
        ingestion::ModelDefinition metadata;
        metadata.run_id = options.run_id;
        metadata.model_id = contracts::ModelId{"wmc-model:" + manifest.data_sha256.substr(0, 16)};
        metadata.code_build_id = options.code_build_id;
        metadata.partition_id = input.partition_id;
        metadata.partition = input.partition;
        metadata.limits.max_facts = 20;
        metadata.limits.max_worlds = 1ULL << 20U;
        metadata.limits.max_evidence = 10'000;
        metadata.limits.max_factor_entries = 1'000'000;
        metadata.limits.max_operations = 50'000'000;
        ingestion::FormulaProvenance provenance{input.source_id, input.observed_at_utc,
            input.original_input_ref, input.extractor_version};
        auto adapted = ingestion::ingest_weighted_cnf(instance, std::move(metadata), provenance);
        if (!adapted.model) {
            result.status = adapted.status;
            result.input_diagnostic = adapted.diagnostic;
            append_event(manifest, "factual_ingestion", ModuleDisposition::failed, result.status,
                         "Weighted CNF to provenance-bearing finite model mapping failed closed.");
            result.semantic_sha256 = semantic_digest(result);
            return result;
        }
        auto model = std::move(*adapted.model);
        for (const auto& evidence : input.additional_evidence) {
            const auto inserted = model.add_evidence(evidence);
            if (!inserted.accepted()) {
                result.status = inserted.status;
                result.input_diagnostic = inserted.diagnostic;
                append_event(manifest, "factual_ingestion", ModuleDisposition::failed, result.status,
                    "Additional evidence rejected; correlated/unknown dependence or partition errors are not coerced.");
                result.factual_model_sha256 = sha256_hex(model.serialize());
                manifest.model_sha256 = *result.factual_model_sha256;
                result.semantic_sha256 = semantic_digest(result);
                return result;
            }
        }
        if (input.query_fact_id) {
            const auto query = model.query(*input.query_fact_id);
            result.factual_query_status = query.status;
            if (query.status != contracts::Status::success) {
                result.status = query.status;
                result.input_diagnostic = query.diagnostic;
                append_event(manifest, "factual_ingestion", ModuleDisposition::failed, result.status,
                    "Requested factual query was unknown, inconsistent, unsupported, or resource-limited; status is preserved.");
                result.factual_model_sha256 = sha256_hex(model.serialize());
                manifest.model_sha256 = *result.factual_model_sha256;
                result.semantic_sha256 = semantic_digest(result);
                return result;
            }
        }
        result.factual_model_sha256 = sha256_hex(model.serialize());
        manifest.model_sha256 = *result.factual_model_sha256;
        append_event(manifest, "factual_ingestion", ModuleDisposition::executed, contracts::Status::success,
            "Exact Boolean priors and clause factors were recorded with source, partition, parser version, and duplicate-clause links.");
    }

    const auto rss_before = peak_rss_bytes();
    if (options.solver_options.timeout_ms == 0 || options.cpu_budget_ms == 0) {
        result.status = contracts::Status::timeout;
        result.input_diagnostic = "Run deadline is already expired under the Phase 8 timeout convention.";
        append_event(manifest, "exact_wmc", ModuleDisposition::failed, result.status,
                     "Expired run deadline prevented solver entry; no partial count exists.");
        result.semantic_sha256 = semantic_digest(result);
        return result;
    }
    try {
        auto solver_options = options.solver_options;
        solver_options.node_limit = solver_node_limit;
        solver_options.timeout_ms = std::min(solver_options.timeout_ms, options.cpu_budget_ms);
        manifest.solver_limits.timeout_ms = solver_options.timeout_ms;
        const auto solved = wmc::exact_wmc(instance, solver_options);
        result.exact_count = solved.count;
        result.satisfiable = solved.satisfiable;
        result.exactness = contracts::Exactness::exact;
        manifest.resources.solver_recursive_nodes = solved.stats.recursive_nodes;
        manifest.resources.elapsed_wall_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
        const auto rss_after = peak_rss_bytes();
        manifest.resources.process_peak_rss_bytes = rss_after ? rss_after : rss_before;
        const auto cpu_after = process_cpu_ms();
        if (cpu_started && cpu_after && *cpu_after >= *cpu_started)
            manifest.resources.process_cpu_ms = *cpu_after - *cpu_started;
        append_event(manifest, "exact_wmc", ModuleDisposition::executed, contracts::Status::success,
            "Exact rational WMC completed; no partial-result path is exposed.");
    } catch (const wmc::ResourceLimit& error) {
        const std::string message = error.what();
        result.status = message.find("time limit") != std::string::npos ?
            contracts::Status::timeout : contracts::Status::resource_limit;
        result.input_diagnostic = message;
        append_event(manifest, "exact_wmc", ModuleDisposition::failed, result.status,
                     "Solver limit ended the run without returning a partial count.");
        manifest.resources.elapsed_wall_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
        manifest.resources.process_peak_rss_bytes = peak_rss_bytes();
        const auto cpu_after = process_cpu_ms();
        if (cpu_started && cpu_after && *cpu_after >= *cpu_started)
            manifest.resources.process_cpu_ms = *cpu_after - *cpu_started;
        result.semantic_sha256 = semantic_digest(result);
        return result;
    } catch (const std::exception& error) {
        result.status = contracts::Status::invalid_schema;
        result.input_diagnostic = std::string("Solver rejected the typed instance: ") + error.what();
        append_event(manifest, "exact_wmc", ModuleDisposition::failed, result.status,
                     "Typed solver validation failed; no fallback result was fabricated.");
        result.semantic_sha256 = semantic_digest(result);
        return result;
    }

    const auto result_value = Value{Object{
        {"count", rational_value(*result.exact_count)},
        {"satisfiable", Value{*result.satisfiable}},
        {"status", Value{"success"}},
        {"solver_version", Value{manifest.solver_version}}}};
    const std::string claim_sha = sha256_hex(contracts::encode_canonical_value(result_value));
    decision::DecisionRequest decision_request;
    decision_request.subject_sha256 = manifest.data_sha256;
    decision_request.states = {{"exact", wmc::Rational{1}, false},
                               {"incorrect_or_incomplete", wmc::Rational{0}, false}};
    decision_request.upstream_status = upstream_status;
    decision_request.output_exactness = contracts::Exactness::exact;
    decision_request.lineage = {"observation:" + input.observation_id.value(),
        "source:" + input.source_id.value(), "model:" +
        (result.factual_model_sha256 ? result.factual_model_sha256->substr(0, 16) : manifest.model_sha256.substr(0, 16))};
    decision_request.assumptions = {
        "The state distribution is a declared deterministic harness condition, not benchmark-calibrated reliability.",
        "The selected exact-result action remains certificate-gated."};
    decision_request.limitations = {
        "The configured verifier must independently establish the exact-rational WMC property.",
        "No production WMC proof verifier or content resolver is implemented in this phase."};
    auto resource_cpu_ms = manifest.resources.process_cpu_ms.value_or(600'001);
    if (resource_cpu_ms > options.cpu_budget_ms) resource_cpu_ms = 600'001;
    auto peak_memory = manifest.resources.process_peak_rss_bytes.value_or(4ULL * 1024ULL * 1024ULL * 1024ULL + 1);
    if (peak_memory > options.memory_budget_bytes) peak_memory = 4ULL * 1024ULL * 1024ULL * 1024ULL + 1;
    const auto cost = decision::normalized_wmc_resource_cost(resource_cpu_ms, peak_memory);
    auto action = decision::make_wmc_exact_result_action(decision_request.states, cost,
        result_value, claim_sha, options.certificate);
    decision_request.actions.push_back(std::move(action));
    auto policy = options.decision_policy;
    if (verifier) {
        policy.verifier_id = std::string(verifier->id());
        policy.verifier_version = std::string(verifier->version());
    }
    result.decision_output = decision::decide(decision_request, policy, verifier);
    if (result.decision_output->status == decision::DecisionStatus::selected &&
        upstream_status == contracts::Status::success) {
        result.status = contracts::Status::success;
        result.exactness = contracts::Exactness::exact;
        append_event(manifest, "decision_output", ModuleDisposition::executed, result.status,
            "Certificate verifier accepted the exact-result action; structured output is selected.");
    } else if (result.decision_output->status == decision::DecisionStatus::abstention) {
        result.status = upstream_status == contracts::Status::success ?
            contracts::Status::abstention : upstream_status;
        result.exact_count.reset();
        result.satisfiable.reset();
        result.exactness = contracts::Exactness::not_applicable;
        append_event(manifest, "decision_output", ModuleDisposition::executed, result.status,
            "Decision gate abstained; solver-stage exactness is not promoted to a selected answer.");
    } else {
        result.status = contracts::Status::invalid_schema;
        result.exact_count.reset();
        result.satisfiable.reset();
        result.exactness = contracts::Exactness::not_applicable;
        append_event(manifest, "decision_output", ModuleDisposition::failed, result.status,
            "Decision request failed validation; no answer was selected.");
    }
    manifest.resources.elapsed_wall_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
    result.semantic_sha256 = semantic_digest(result);
    return result;
}

RunResult execute(const StructuredWmcInput& input, const RunOptions& options,
                  const decision::CertificateVerifier* verifier) {
    const auto started = Clock::now();
    const auto cpu_started = process_cpu_ms();
    auto result = execute_internal(input, options, verifier, started, cpu_started);
    result.manifest.resources.elapsed_wall_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
    const auto cpu_finished = process_cpu_ms();
    if (cpu_started && cpu_finished && *cpu_finished >= *cpu_started)
        result.manifest.resources.process_cpu_ms = *cpu_finished - *cpu_started;
    result.semantic_sha256 = semantic_digest(result);
    return result;
}

std::string serialize(const RunResult& result) {
    Object root{{"exactness", Value{std::string(contracts::to_string(result.exactness))}},
        {"input_diagnostic", Value{result.input_diagnostic}},
        {"manifest", manifest_value(result.manifest)},
        {"output_schema_id", Value{std::string(kOutputSchemaId)}},
        {"output_schema_sha256", Value{result.manifest.output_schema_sha256}},
        {"schema_id", Value{std::string(kRunSchemaId)}},
        {"schema_version", Value{kSchemaVersion}},
        {"semantic_sha256", Value{result.semantic_sha256}},
        {"status", Value{status_name(result.status)}}};
    root.emplace("satisfiable", result.satisfiable ? Value{*result.satisfiable} : Value{nullptr});
    root.emplace("exact_count", result.exact_count ? rational_value(*result.exact_count) : Value{nullptr});
    root.emplace("factual_model_sha256", result.factual_model_sha256 ?
        Value{*result.factual_model_sha256} : Value{nullptr});
    root.emplace("factual_query_status", result.factual_query_status ?
        Value{std::string(contracts::to_string(*result.factual_query_status))} : Value{nullptr});
    root.emplace("decision", result.decision_output ? decision_value(*result.decision_output) : Value{nullptr});
    return contracts::encode_canonical_value(Value{std::move(root)});
}

}  // namespace xai::orchestration
