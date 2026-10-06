#pragma once

#include "xai/contracts.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace xai::learning {

enum class CompletionOutcome { completed_exact, explicit_noncompletion, non_result };

struct LearningLimits {
    std::uint64_t max_examples{10'000};
    std::uint64_t max_audit_entries{10'000};
    std::uint64_t max_contexts{10'000};
};

struct PredictorDefinition {
    contracts::RunId run_id{"run-learning"};
    contracts::ModelId library_id{"library:wmc-completion-v1"};
    contracts::SchemaId result_schema_id{"xai.predictive_prediction"};
    contracts::SchemaId state_schema_id{"xai.predictive_state"};
    contracts::CodeBuildId code_build_id{"local-build"};
    LearningLimits limits;
};

// Counts are exact parser-derived features. All three are required; missing
// values are unsupported rather than imputed or silently mapped to zero.
struct PredictionInput {
    contracts::ObservationId observation_id;
    contracts::ResultId result_id;
    contracts::PartitionId partition_id;
    contracts::DataPartition partition{contracts::DataPartition::not_applicable};
    contracts::SchemaId feature_schema_id{"xai.wmc_structure"};
    std::uint32_t feature_schema_version{1};
    bool phase0_eligible{false};
    std::optional<std::uint64_t> variables;
    std::optional<std::uint64_t> clauses;
    std::optional<std::uint64_t> literal_occurrences;
};

struct PredictionSummary {
    long double completion_probability{0.5L};
    std::array<long double, 3> expert_completion_probability{};
    std::array<long double, 3> model_weights_before{};
    std::array<long double, 3> model_weights_after{};
    std::optional<long double> observed_log_probability;
    long double cumulative_mixture_log_score{0.0L};
    std::uint64_t accepted_examples{0};
};

struct LearningResult {
    contracts::Status status{contracts::Status::unknown};
    std::string diagnostic;
    std::optional<PredictionSummary> prediction;
    contracts::RecordEnvelope record;
    bool state_updated{false};
};

struct TrainingUseDecision {
    contracts::ObservationId observation_id;
    contracts::PartitionId partition_id;
    contracts::DataPartition partition{contracts::DataPartition::not_applicable};
    contracts::Status status{contracts::Status::unknown};
    bool accepted{false};
    std::string reason;
};

class SequentialPredictor {
public:
    struct Counts { std::uint64_t positive{0}; std::uint64_t negative{0}; };

    explicit SequentialPredictor(PredictorDefinition definition = {});

    // Prediction is read-only and can be requested for any named data
    // partition. Only observe() can update sufficient statistics.
    [[nodiscard]] LearningResult predict(const PredictionInput& input) const;
    [[nodiscard]] LearningResult observe(const PredictionInput& input,
                                         CompletionOutcome outcome);

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static SequentialPredictor restore(std::string_view canonical_record);

    [[nodiscard]] const PredictorDefinition& definition() const noexcept { return definition_; }
    [[nodiscard]] std::uint64_t accepted_examples() const noexcept { return accepted_examples_; }
    [[nodiscard]] long double cumulative_mixture_log_score() const noexcept {
        return cumulative_mixture_log_score_;
    }
    [[nodiscard]] const std::vector<TrainingUseDecision>& training_use_audit() const noexcept {
        return training_use_audit_;
    }
    [[nodiscard]] const std::string& model_library_sha256() const noexcept { return library_sha256_; }
    [[nodiscard]] const std::string& model_prior_sha256() const noexcept { return prior_sha256_; }

private:
    PredictorDefinition definition_;
    Counts global_counts_;
    std::map<std::string, Counts, std::less<>> context_counts_;
    std::array<Counts, 3> transition_counts_{};
    std::array<long double, 3> expert_log_scores_{};
    long double cumulative_mixture_log_score_{0.0L};
    std::optional<bool> previous_outcome_;
    std::set<std::string, std::less<>> accepted_observation_ids_;
    std::vector<TrainingUseDecision> training_use_audit_;
    bool streaming_started_{false};
    std::uint64_t accepted_examples_{0};
    std::string library_sha256_;
    std::string prior_sha256_;

    [[nodiscard]] LearningResult failure(const PredictionInput& input,
                                         contracts::Status status,
                                         std::string message) const;
    void append_audit(const PredictionInput& input, contracts::Status status,
                      bool accepted, std::string reason);
};

[[nodiscard]] std::string_view to_string(CompletionOutcome value) noexcept;
[[nodiscard]] std::string_view model_library_sha256() noexcept;
[[nodiscard]] std::string_view model_prior_sha256() noexcept;

}  // namespace xai::learning
