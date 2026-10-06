#pragma once

#include "xai/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gmpxx.h>
#include "xai/wmc.hpp"

namespace xai::ingestion {

using Rational = mpq_class;

enum class IdentityResolution { resolved, unresolved };
enum class WorldSemantics { open_world };
enum class EvidenceSemantics { likelihood, hard_constraint };
enum class DependenceClass { independent, model_factor, exact_duplicate, correlated, unknown };
enum class DependencyRelation { exact_duplicate, correlated, unknown };

struct IdentityReference {
    IdentityResolution resolution{IdentityResolution::unresolved};
    std::optional<contracts::EntityId> entity_id;
    std::vector<contracts::EntityId> candidate_entity_ids;
};

struct FactDefinition {
    contracts::FactId fact_id;
    std::string predicate;
    IdentityReference subject;
};

// These are nonnegative exact potentials. They need not sum to one: the
// normalized prior is obtained from the finite model's prior normalizer.
struct LiteralPrior {
    contracts::FactId fact_id;
    Rational false_weight{1};
    Rational true_weight{1};
};

struct EvidenceLink {
    contracts::EvidenceId evidence_id;
    DependencyRelation relation{DependencyRelation::unknown};
};

// Evidence is append-only in BeliefModel. The likelihood table is indexed by
// the scope order, with scope[0] as the least-significant assignment bit.
struct EvidenceRecord {
    contracts::EvidenceId evidence_id;
    contracts::SourceId source_id;
    std::string observed_at_utc;
    std::string original_observation_ref;
    std::string extractor_version;
    contracts::DataPartition partition{contracts::DataPartition::development};
    std::vector<contracts::FactId> scope;
    EvidenceSemantics semantics{EvidenceSemantics::likelihood};
    std::vector<Rational> likelihood;
    DependenceClass dependence{DependenceClass::unknown};
    std::vector<EvidenceLink> dependencies;
};

struct ResourceLimits {
    std::size_t max_facts{63};
    std::uint64_t max_worlds{1'048'576};
    std::size_t max_evidence{10'000};
    std::uint64_t max_factor_entries{1'000'000};
    std::uint64_t max_operations{50'000'000};
    std::size_t max_rational_bits{512};
};

struct ModelDefinition {
    contracts::RunId run_id{"run-ingestion"};
    contracts::ModelId model_id{"belief-model-v1"};
    contracts::SchemaId schema_id{"xai.factual_model"};
    contracts::CodeBuildId code_build_id{"local-build"};
    contracts::PartitionId partition_id{"mcc24-public-even"};
    contracts::DataPartition partition{contracts::DataPartition::development};
    WorldSemantics world_semantics{WorldSemantics::open_world};
    std::vector<FactDefinition> facts;
    std::vector<LiteralPrior> priors;
    ResourceLimits limits;
};

struct IngestionResult {
    contracts::Status status{contracts::Status::success};
    std::string diagnostic;
    [[nodiscard]] bool accepted() const noexcept {
        return status == contracts::Status::success;
    }
};

struct BeliefQuery {
    contracts::Status status{contracts::Status::unknown};
    std::optional<Rational> prior_normalizer;
    std::optional<Rational> evidence_normalizer;
    std::optional<Rational> conditional_normalizer;
    std::optional<Rational> probability_true;
    std::vector<contracts::EvidenceId> evidence_lineage;
    std::uint64_t operations{0};
    std::string diagnostic;
};

class BeliefModel {
public:
    explicit BeliefModel(ModelDefinition definition);

    // Evidence is copied into the ledger only after full validation. The ledger
    // exposes const accessors and has no update/delete operation.
    [[nodiscard]] IngestionResult add_evidence(EvidenceRecord evidence);
    [[nodiscard]] BeliefQuery query(const contracts::FactId& fact_id) const;
    [[nodiscard]] contracts::RecordEnvelope query_record(
        const contracts::QueryId& query_id,
        const contracts::ResultId& result_id,
        const contracts::FactId& fact_id) const;

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static BeliefModel restore(std::string_view canonical_record);

    [[nodiscard]] const ModelDefinition& definition() const noexcept { return definition_; }
    [[nodiscard]] const std::vector<EvidenceRecord>& evidence() const noexcept { return evidence_; }

private:
    ModelDefinition definition_;
    std::map<std::string, std::size_t, std::less<>> fact_indices_;
    std::vector<EvidenceRecord> evidence_;
    std::uint64_t factor_entries_{0};

    [[nodiscard]] std::vector<std::string> validate_definition() const;
    [[nodiscard]] std::vector<std::string> validate_evidence(const EvidenceRecord& evidence) const;
    [[nodiscard]] contracts::RecordEnvelope model_record() const;
};

struct FormulaProvenance {
    contracts::SourceId source_id{"source:cnf"};
    std::string observed_at_utc;
    std::string original_observation_ref;
    std::string extractor_version;
};

struct FormulaIngestionResult {
    contracts::Status status{contracts::Status::invalid_schema};
    std::optional<BeliefModel> model;
    std::string diagnostic;
};

// Translate a parsed, unprojected weighted CNF into Boolean fact priors and
// deterministic clause factors. Clause-factor multiplication means conjunction
// by the CNF model contract; it is not a claim of independent corroborating sources.
[[nodiscard]] FormulaIngestionResult ingest_weighted_cnf(
    const xai::wmc::Instance& instance,
    ModelDefinition model_metadata,
    const FormulaProvenance& provenance);

[[nodiscard]] std::string_view to_string(IdentityResolution value) noexcept;
[[nodiscard]] std::string_view to_string(WorldSemantics value) noexcept;
[[nodiscard]] std::string_view to_string(EvidenceSemantics value) noexcept;
[[nodiscard]] std::string_view to_string(DependenceClass value) noexcept;
[[nodiscard]] std::string_view to_string(DependencyRelation value) noexcept;

}  // namespace xai::ingestion
