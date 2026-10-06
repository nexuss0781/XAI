#pragma once

#include "xai/adaptation.hpp"
#include "xai/contracts.hpp"
#include "xai/decision.hpp"
#include "xai/ingestion.hpp"
#include "xai/wmc.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xai::orchestration {

inline constexpr std::string_view kInputSchemaId = "xai.phase8.structured-wmc-input";
inline constexpr std::string_view kRunSchemaId = "xai.phase8.run-manifest";
inline constexpr std::string_view kOutputSchemaId = "xai.phase8.run-output";
inline constexpr std::uint32_t kSchemaVersion = 1;
inline constexpr std::string_view kParserVersion = "xai-dimacs-wmc-parser-v1";
inline constexpr std::string_view kSolverVersion = "xai-exact-wmc-dpll-v1";
inline constexpr std::string_view kPartitionId = "mcc24-public-even";

struct IdentityBinding {
    std::uint32_t variable_index{0};
    ingestion::IdentityReference identity;
    bool required_for_task{false};
};

// The supported route is a caller-provided, structured, unprojected DIMACS-WMC
// record. original_input_ref is an opaque reference; raw formula text and parser
// token details are not copied into the run report or ordinary diagnostics.
struct StructuredWmcInput {
    contracts::ObservationId observation_id;
    contracts::SourceId source_id;
    contracts::PartitionId partition_id{std::string(kPartitionId)};
    contracts::DataPartition partition{contracts::DataPartition::development};
    std::string original_input_ref;
    std::string observed_at_utc;
    std::string extractor_version{std::string(kParserVersion)};
    std::string formula_text;
    std::vector<std::string> uncertainty_codes;
    std::vector<std::string> rejected_span_refs;
    std::vector<IdentityBinding> identity_bindings;
    std::vector<ingestion::EvidenceRecord> additional_evidence;
    std::optional<contracts::FactId> query_fact_id;
};

struct AdaptationScenario {
    adaptation::ControllerOptions controller_options;
    adaptation::DevelopmentSuite development_suite;
    adaptation::StreamingObservation streaming_observation;
};

struct RunOptions {
    contracts::RunId run_id{"run:phase8"};
    contracts::CodeBuildId code_build_id{"phase8-build"};
    wmc::ParseLimits parse_limits;
    wmc::SolverOptions solver_options;
    std::uint64_t cpu_budget_ms{600'000};
    std::uint64_t memory_budget_bytes{4ULL * 1024ULL * 1024ULL * 1024ULL};
    std::optional<std::uint64_t> seed;
    std::optional<AdaptationScenario> adaptation;
    std::optional<decision::Certificate> certificate;
    decision::DecisionPolicy decision_policy = [] {
        decision::DecisionPolicy policy;
        policy.verifier_id = "verifier:unconfigured";
        policy.verifier_version = "0";
        return policy;
    }();
};

enum class ModuleDisposition { executed, skipped, failed };

struct ModuleEvent {
    std::string module;
    ModuleDisposition disposition{ModuleDisposition::skipped};
    contracts::Status status{contracts::Status::unknown};
    std::string reason;
};

struct ResourceMeasurement {
    std::uint64_t elapsed_wall_ms{0};
    std::optional<std::uint64_t> process_cpu_ms;
    std::optional<std::uint64_t> process_peak_rss_bytes;
    std::uint64_t solver_recursive_nodes{0};
};

struct RunManifest {
    contracts::RunId run_id;
    contracts::ObservationId observation_id;
    contracts::SourceId source_id;
    contracts::PartitionId partition_id;
    contracts::DataPartition partition{contracts::DataPartition::not_applicable};
    std::string original_input_ref;
    std::uint64_t input_bytes{0};
    std::string code_sha256;
    std::string data_sha256;
    std::string supplemental_input_sha256;
    std::string model_sha256;
    std::string config_sha256;
    std::string input_schema_sha256;
    std::string run_schema_sha256;
    std::string output_schema_sha256;
    std::string code_build_id;
    std::string compiler_id;
    std::string compiler_version;
    std::string build_type;
    std::string verifier_id;
    std::string verifier_version;
    std::string parser_version;
    std::string extractor_version;
    std::string solver_version;
    std::optional<std::uint64_t> seed;
    wmc::ParseLimits parse_limits;
    wmc::SolverOptions solver_limits;
    std::uint64_t cpu_budget_ms{0};
    std::uint64_t memory_budget_bytes{0};
    ResourceMeasurement resources;
    std::vector<ModuleEvent> modules;
    std::vector<IdentityBinding> identity_bindings;
    std::vector<std::string> uncertainty_codes;
    std::vector<std::string> rejected_span_refs;
    std::vector<adaptation::AuditEvent> adaptation_audit;
};

struct RunResult {
    contracts::Status status{contracts::Status::unknown};
    contracts::Exactness exactness{contracts::Exactness::not_applicable};
    std::optional<bool> satisfiable;
    std::optional<wmc::Rational> exact_count;
    std::optional<decision::DecisionOutput> decision_output;
    std::optional<std::string> factual_model_sha256;
    std::optional<contracts::Status> factual_query_status;
    std::string input_diagnostic;
    std::string semantic_sha256;
    RunManifest manifest;
};

// SHA-256 implementation used for content-addressed run records. It is tested
// against standard test vectors and avoids introducing a crypto-library dependency.
[[nodiscard]] std::string sha256_hex(std::string_view bytes);

// Executes one deterministic in-process run. A final-test/unknown partition is
// rejected before formula bytes are hashed or parsed. Missing/failed required
// stages are never converted to success. A selected answer requires the supplied
// certificate and verifier to agree with the Phase 7 policy.
[[nodiscard]] RunResult execute(const StructuredWmcInput& input,
                                const RunOptions& options = {},
                                const decision::CertificateVerifier* verifier = nullptr);

// Canonical, versioned JSON serialization. Input contents are represented only by
// hashes and the caller's opaque source reference.
[[nodiscard]] std::string serialize(const RunResult& result);

[[nodiscard]] std::string_view to_string(ModuleDisposition value) noexcept;

}  // namespace xai::orchestration
