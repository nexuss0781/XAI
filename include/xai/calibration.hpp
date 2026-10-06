#pragma once

#include "xai/contracts.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xai::calibration {

enum class EvidenceKind { synthetic_fixture, authorized_evaluation };

[[nodiscard]] std::string_view to_string(EvidenceKind value) noexcept;

struct CalibrationProtocol {
    std::string protocol_id{"xai.phase6.calibration"};
    std::string protocol_version{"1"};
    contracts::ModelId model_id{"model:synthetic-forecast-fixture-v1"};
    contracts::CodeBuildId code_build_id{"phase6-calibration-v1"};
    contracts::PartitionId calibration_partition_id{"partition:phase6-calibration-v1"};
    std::string dataset_id{"synthetic-wmc-completion-fixture"};
    std::string dataset_version{"synthetic-wmc-completion-v1"};
    std::string selection_policy{"fixed, version-controlled synthetic fixture; no corpus rows"};
    EvidenceKind evidence_kind{EvidenceKind::synthetic_fixture};
    std::vector<std::string> fitting_observation_ids;
    std::vector<std::string> calibration_observation_ids;
    std::vector<std::string> final_test_observation_ids;
    std::uint32_t reliability_bin_count{5};
    std::uint64_t minimum_subgroup_size{5};
    std::uint64_t max_observations{10'000};
    std::uint64_t max_subgroup_label_bytes{256};
    long double conformal_alpha{0.1L};
    bool exchangeability_defensible{false};
    std::string exchangeability_basis;
};

struct Forecast {
    contracts::ObservationId observation_id;
    contracts::PartitionId partition_id;
    contracts::DataPartition partition{contracts::DataPartition::not_applicable};
    long double completion_probability{0.5L};
    std::optional<bool> completed_exact;
    std::string subgroup;
    bool shift_flag{false};
};

struct ScoreInterval {
    long double lower{0.0L};
    long double upper{0.0L};
};

struct ProperScoreSummary {
    std::uint64_t sample_count{0};
    long double mean{0.0L};
    std::optional<long double> standard_error;
    std::optional<ScoreInterval> normal_approx_95_interval;
};

struct SharpnessSummary {
    std::uint64_t forecast_count{0};
    long double mean_completion_probability{0.0L};
    long double mean_confidence{0.0L};
    long double mean_predictive_entropy_nats{0.0L};
    long double minimum_probability{0.0L};
    long double maximum_probability{0.0L};
    std::vector<std::uint64_t> probability_bin_counts;
};

struct ReliabilityBin {
    long double lower_edge{0.0L};
    long double upper_edge{1.0L};
    bool upper_edge_inclusive{false};
    std::uint64_t count{0};
    std::optional<long double> mean_forecast;
    std::optional<long double> observed_rate;
    std::optional<ScoreInterval> wilson_95_interval;
};

struct RiskCoveragePoint {
    long double minimum_confidence{0.0L};
    std::uint64_t retained_count{0};
    long double coverage{0.0L};
    long double selective_risk{0.0L};
};

struct SubgroupCount {
    std::uint64_t total_count{0};
    std::uint64_t scored_count{0};
    std::uint64_t missing_outcome_count{0};
    std::uint64_t error_count{0};
    bool below_minimum_sample_size{false};
};

enum class ConformalStatus { evaluated, unsupported_assumption, no_outcomes };

struct ConformalThreshold {
    ConformalStatus status{ConformalStatus::no_outcomes};
    long double nominal_miscoverage{0.1L};
    std::uint64_t calibration_sample_count{0};
    std::uint64_t order_statistic_index{0};
    std::optional<long double> nonconformity_threshold;
    bool threshold_unbounded{false};
    bool conditional_coverage_statement{false};
    std::string method;
    std::string assumption;
    std::string limitation;
};

struct BinaryPredictionSet {
    bool include_noncompletion{false};
    bool include_completion{false};
    [[nodiscard]] bool empty() const noexcept {
        return !include_noncompletion && !include_completion;
    }
};

struct CalibrationReport {
    contracts::Status status{contracts::Status::success};
    std::string diagnostic;
    std::string protocol_id;
    std::string protocol_version;
    std::string dataset_id;
    std::string dataset_version;
    std::string selection_policy;
    std::string evidence_kind;
    std::string model_id;
    std::string code_build_id;
    std::string partition_id;
    std::string partition;
    std::uint64_t observation_count{0};
    std::uint64_t scored_count{0};
    std::uint64_t missing_outcome_count{0};
    std::uint64_t shift_flagged_count{0};
    std::uint64_t shift_flagged_scored_count{0};
    std::optional<ProperScoreSummary> brier_score;
    std::optional<ProperScoreSummary> logarithmic_score;
    SharpnessSummary sharpness;
    std::vector<ReliabilityBin> reliability_bins;
    std::vector<RiskCoveragePoint> risk_coverage;
    std::map<std::string, SubgroupCount, std::less<>> subgroups;
    ConformalThreshold conformal;
    std::vector<std::string> assumptions;
    std::vector<std::string> limitations;
};

// All submitted forecasts must be exactly the versioned calibration partition.
// Partition labels are caller-supplied metadata, not authenticated provenance.
[[nodiscard]] CalibrationReport evaluate(const CalibrationProtocol& protocol,
                                         const std::vector<Forecast>& forecasts);

// Returns no set when the split-conformal assumptions were not authorized.
[[nodiscard]] std::optional<BinaryPredictionSet> make_binary_prediction_set(
    long double completion_probability, const ConformalThreshold& threshold);

}  // namespace xai::calibration
