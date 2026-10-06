#include "xai/calibration.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xai::calibration {
namespace {
constexpr long double kWilsonZ = 1.95996398454005423552L;
constexpr long double kNormalZ = 1.95996398454005423552L;

[[noreturn]] void invalid(const std::string& message) {
    throw std::invalid_argument(message);
}

void validate_id_list(const std::vector<std::string>& ids, const std::string& label,
                      std::set<std::string, std::less<>>& all_ids) {
    for (const auto& id : ids) {
        if (!contracts::ObservationId{id}.valid()) invalid(label + " contains an invalid observation ID");
        if (!all_ids.insert(id).second) invalid("observation IDs overlap or repeat across partitions");
    }
}

void validate_protocol(const CalibrationProtocol& protocol) {
    if (protocol.protocol_id.empty() || protocol.protocol_version.empty() ||
        !protocol.model_id.valid() || !protocol.code_build_id.valid() ||
        !protocol.calibration_partition_id.valid() || protocol.dataset_id.empty() ||
        protocol.dataset_version.empty() || protocol.selection_policy.empty())
        invalid("calibration protocol metadata is incomplete or invalid");
    if (to_string(protocol.evidence_kind) == "invalid")
        invalid("calibration evidence kind is invalid");
    if (protocol.calibration_observation_ids.empty())
        invalid("the frozen calibration partition must name at least one observation");
    if (protocol.reliability_bin_count == 0 || protocol.reliability_bin_count > 100)
        invalid("reliability bin count must be in [1, 100]");
    if (protocol.minimum_subgroup_size == 0 || protocol.max_observations == 0 ||
        protocol.max_subgroup_label_bytes == 0 || protocol.max_subgroup_label_bytes > 1024)
        invalid("sample-size and observation limits must be positive");
    if (protocol.calibration_observation_ids.size() > protocol.max_observations)
        invalid("frozen calibration partition exceeds the configured observation limit");
    if (!std::isfinite(protocol.conformal_alpha) || protocol.conformal_alpha <= 0.0L ||
        protocol.conformal_alpha >= 1.0L)
        invalid("conformal miscoverage alpha must be finite and in (0, 1)");
    if (protocol.exchangeability_defensible && protocol.exchangeability_basis.empty())
        invalid("a defensible exchangeability declaration requires an explicit basis");
    std::set<std::string, std::less<>> all_ids;
    validate_id_list(protocol.fitting_observation_ids, "fitting partition", all_ids);
    validate_id_list(protocol.calibration_observation_ids, "calibration partition", all_ids);
    validate_id_list(protocol.final_test_observation_ids, "final-test partition", all_ids);
}

[[nodiscard]] ScoreInterval wilson_interval(std::uint64_t successes, std::uint64_t count) {
    const auto n = static_cast<long double>(count);
    const auto proportion = static_cast<long double>(successes) / n;
    const auto z2 = kWilsonZ * kWilsonZ;
    const auto denominator = 1.0L + z2 / n;
    const auto center = (proportion + z2 / (2.0L * n)) / denominator;
    const auto radius = kWilsonZ * std::sqrt(
        proportion * (1.0L - proportion) / n + z2 / (4.0L * n * n)) / denominator;
    return {std::max(0.0L, center - radius), std::min(1.0L, center + radius)};
}

[[nodiscard]] ProperScoreSummary summarize(const std::vector<long double>& values) {
    ProperScoreSummary summary;
    summary.sample_count = static_cast<std::uint64_t>(values.size());
    if (values.empty()) return summary;
    const auto infinite = std::any_of(values.begin(), values.end(),
        [](long double value) { return !std::isfinite(value); });
    if (infinite) {
        summary.mean = std::numeric_limits<long double>::infinity();
        return summary;
    }
    const auto sum = std::accumulate(values.begin(), values.end(), 0.0L);
    summary.mean = sum / static_cast<long double>(values.size());
    if (values.size() >= 2) {
        long double squares = 0.0L;
        for (const auto value : values) {
            const auto difference = value - summary.mean;
            squares += difference * difference;
        }
        const auto sample_variance = squares / static_cast<long double>(values.size() - 1U);
        const auto standard_error = std::sqrt(sample_variance / static_cast<long double>(values.size()));
        summary.standard_error = standard_error;
        summary.normal_approx_95_interval = ScoreInterval{
            std::max(0.0L, summary.mean - kNormalZ * standard_error),
            summary.mean + kNormalZ * standard_error};
    }
    return summary;
}

[[nodiscard]] bool predicted_completion(long double probability) noexcept {
    return probability >= 0.5L;
}

[[nodiscard]] long double confidence(long double probability) noexcept {
    return std::max(probability, 1.0L - probability);
}

[[nodiscard]] std::string subgroup_name(const std::string& subgroup) {
    return subgroup.empty() ? "unspecified" : subgroup;
}

[[nodiscard]] std::uint64_t order_index(std::uint64_t sample_count, long double alpha) {
    const auto raw = std::ceil((static_cast<long double>(sample_count) + 1.0L) * (1.0L - alpha));
    if (raw < 1.0L || raw > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        invalid("conformal order-statistic index is out of range");
    return static_cast<std::uint64_t>(raw);
}
}  // namespace

std::string_view to_string(EvidenceKind value) noexcept {
    switch (value) {
        case EvidenceKind::synthetic_fixture: return "synthetic_fixture";
        case EvidenceKind::authorized_evaluation: return "authorized_evaluation";
    }
    return "invalid";
}

CalibrationReport evaluate(const CalibrationProtocol& protocol,
                           const std::vector<Forecast>& forecasts) {
    validate_protocol(protocol);
    if (forecasts.size() != protocol.calibration_observation_ids.size())
        invalid("forecast count does not match the frozen calibration partition manifest");

    std::set<std::string, std::less<>> expected(protocol.calibration_observation_ids.begin(),
                                               protocol.calibration_observation_ids.end());
    std::set<std::string, std::less<>> observed;
    for (const auto& forecast : forecasts) {
        if (!forecast.observation_id.valid() || !forecast.partition_id.valid())
            invalid("forecast is missing a valid observation or partition ID");
        if (forecast.partition != contracts::DataPartition::calibration ||
            forecast.partition_id != protocol.calibration_partition_id)
            invalid("only records labeled with the frozen calibration partition are authorized");
        if (!std::isfinite(forecast.completion_probability) ||
            forecast.completion_probability < 0.0L || forecast.completion_probability > 1.0L)
            invalid("forecast probability must be finite and in [0, 1]");
        if (forecast.subgroup.size() > protocol.max_subgroup_label_bytes)
            invalid("subgroup label exceeds the configured byte limit");
        const auto& id = forecast.observation_id.value();
        if (!expected.contains(id) || !observed.insert(id).second)
            invalid("forecast IDs must match the frozen calibration IDs exactly, without duplicates");
    }
    if (observed != expected) invalid("one or more frozen calibration observations are missing");

    CalibrationReport report;
    report.protocol_id = protocol.protocol_id;
    report.protocol_version = protocol.protocol_version;
    report.dataset_id = protocol.dataset_id;
    report.dataset_version = protocol.dataset_version;
    report.selection_policy = protocol.selection_policy;
    report.evidence_kind = std::string(to_string(protocol.evidence_kind));
    report.model_id = protocol.model_id.value();
    report.code_build_id = protocol.code_build_id.value();
    report.partition_id = protocol.calibration_partition_id.value();
    report.partition = std::string(contracts::to_string(contracts::DataPartition::calibration));
    report.observation_count = static_cast<std::uint64_t>(forecasts.size());
    report.sharpness.forecast_count = report.observation_count;
    report.sharpness.probability_bin_counts.assign(protocol.reliability_bin_count, 0);
    report.reliability_bins.resize(protocol.reliability_bin_count);
    for (std::uint32_t index = 0; index < protocol.reliability_bin_count; ++index) {
        auto& bin = report.reliability_bins[index];
        bin.lower_edge = static_cast<long double>(index) /
                         static_cast<long double>(protocol.reliability_bin_count);
        bin.upper_edge = static_cast<long double>(index + 1U) /
                         static_cast<long double>(protocol.reliability_bin_count);
        bin.upper_edge_inclusive = index + 1U == protocol.reliability_bin_count;
    }

    std::vector<long double> brier_values;
    std::vector<long double> log_values;
    long double probability_sum = 0.0L;
    long double confidence_sum = 0.0L;
    long double entropy_sum = 0.0L;
    auto minimum_probability = std::numeric_limits<long double>::infinity();
    auto maximum_probability = -std::numeric_limits<long double>::infinity();
    std::vector<std::uint64_t> bin_successes(protocol.reliability_bin_count, 0);
    std::vector<long double> bin_probability_sums(protocol.reliability_bin_count, 0.0L);
    struct RankedCase { long double confidence; bool error; };
    std::vector<RankedCase> ranked;
    std::vector<long double> conformal_scores;

    for (const auto& forecast : forecasts) {
        ++report.subgroups[subgroup_name(forecast.subgroup)].total_count;
        if (forecast.shift_flag) ++report.shift_flagged_count;
        const auto probability = forecast.completion_probability;
        probability_sum += probability;
        confidence_sum += confidence(probability);
        auto entropy = 0.0L;
        if (probability > 0.0L) entropy -= probability * std::log(probability);
        if (probability < 1.0L) entropy -= (1.0L - probability) * std::log1p(-probability);
        entropy_sum += entropy;
        minimum_probability = std::min(minimum_probability, probability);
        maximum_probability = std::max(maximum_probability, probability);
        const auto sharp_bin = std::min(static_cast<std::uint32_t>(
            std::floor(probability * static_cast<long double>(protocol.reliability_bin_count))),
            protocol.reliability_bin_count - 1U);
        ++report.sharpness.probability_bin_counts[sharp_bin];
        if (!forecast.completed_exact.has_value()) {
            ++report.missing_outcome_count;
            ++report.subgroups[subgroup_name(forecast.subgroup)].missing_outcome_count;
            continue;
        }
        ++report.scored_count;
        auto& group = report.subgroups[subgroup_name(forecast.subgroup)];
        ++group.scored_count;
        if (forecast.shift_flag) ++report.shift_flagged_scored_count;
        const auto outcome = *forecast.completed_exact;
        const auto outcome_value = outcome ? 1.0L : 0.0L;
        const auto error = predicted_completion(probability) != outcome;
        if (error) ++group.error_count;
        const auto brier_difference = probability - outcome_value;
        brier_values.push_back(brier_difference * brier_difference);
        if ((outcome && probability == 0.0L) || (!outcome && probability == 1.0L))
            log_values.push_back(std::numeric_limits<long double>::infinity());
        else if (outcome)
            log_values.push_back(-std::log(probability));
        else
            log_values.push_back(-std::log1p(-probability));

        const auto raw_bin = static_cast<std::uint32_t>(
            std::floor(probability * static_cast<long double>(protocol.reliability_bin_count)));
        const auto bin_index = std::min(raw_bin, protocol.reliability_bin_count - 1U);
        auto& bin = report.reliability_bins[bin_index];
        ++bin.count;
        bin_probability_sums[bin_index] += probability;
        if (outcome) ++bin_successes[bin_index];

        ranked.push_back({confidence(probability), error});
        conformal_scores.push_back(1.0L - (outcome ? probability : 1.0L - probability));
    }

    if (!forecasts.empty()) {
        const auto denominator = static_cast<long double>(forecasts.size());
        report.sharpness.mean_completion_probability = probability_sum / denominator;
        report.sharpness.mean_confidence = confidence_sum / denominator;
        report.sharpness.mean_predictive_entropy_nats = entropy_sum / denominator;
        report.sharpness.minimum_probability = minimum_probability;
        report.sharpness.maximum_probability = maximum_probability;
    }

    for (auto& [name, group] : report.subgroups) {
        (void)name;
        group.below_minimum_sample_size = group.scored_count < protocol.minimum_subgroup_size;
    }
    for (std::size_t index = 0; index < report.reliability_bins.size(); ++index) {
        auto& bin = report.reliability_bins[index];
        if (bin.count == 0) continue;
        bin.mean_forecast = bin_probability_sums[index] / static_cast<long double>(bin.count);
        bin.observed_rate = static_cast<long double>(bin_successes[index]) /
                            static_cast<long double>(bin.count);
        bin.wilson_95_interval = wilson_interval(bin_successes[index], bin.count);
    }
    if (!brier_values.empty()) {
        report.brier_score = summarize(brier_values);
        report.logarithmic_score = summarize(log_values);
    } else {
        report.status = contracts::Status::unknown;
        report.diagnostic = "no observed outcomes; proper scores and outcome-based diagnostics are unavailable";
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedCase& left, const RankedCase& right) {
        if (left.confidence != right.confidence) return left.confidence > right.confidence;
        return left.error < right.error;
    });
    std::uint64_t retained = 0;
    std::uint64_t errors = 0;
    std::size_t position = 0;
    while (position < ranked.size()) {
        const auto threshold = ranked[position].confidence;
        auto end = position;
        while (end < ranked.size() && ranked[end].confidence == threshold) {
            ++retained;
            if (ranked[end].error) ++errors;
            ++end;
        }
        report.risk_coverage.push_back({threshold, retained,
            static_cast<long double>(retained) / static_cast<long double>(ranked.size()),
            static_cast<long double>(errors) / static_cast<long double>(retained)});
        position = end;
    }

    report.conformal.nominal_miscoverage = protocol.conformal_alpha;
    report.conformal.calibration_sample_count = static_cast<std::uint64_t>(conformal_scores.size());
    report.conformal.method = "split conformal classification; nonconformity = 1 - probability assigned to the observed class";
    if (conformal_scores.empty()) {
        report.conformal.status = ConformalStatus::no_outcomes;
        report.conformal.limitation = "no labeled calibration outcomes are available";
    } else if (!protocol.exchangeability_defensible) {
        report.conformal.status = ConformalStatus::unsupported_assumption;
        report.conformal.limitation = "no coverage statement or prediction set is emitted because exchangeability was not defended";
    } else {
        std::sort(conformal_scores.begin(), conformal_scores.end());
        report.conformal.order_statistic_index = order_index(report.conformal.calibration_sample_count,
                                                             protocol.conformal_alpha);
        report.conformal.status = ConformalStatus::evaluated;
        report.conformal.conditional_coverage_statement = true;
        report.conformal.assumption = protocol.exchangeability_basis;
        if (report.conformal.order_statistic_index > report.conformal.calibration_sample_count) {
            report.conformal.threshold_unbounded = true;
            report.conformal.nonconformity_threshold = std::numeric_limits<long double>::infinity();
            report.conformal.limitation = "finite-sample k > n boundary: threshold is +infinity and the binary prediction set contains both outcomes";
        } else {
            report.conformal.nonconformity_threshold =
                conformal_scores[static_cast<std::size_t>(report.conformal.order_statistic_index - 1U)];
            report.conformal.limitation = "coverage statement is conditional on the declared exchangeability assumption; no held-out empirical coverage was measured";
        }
    }

    report.assumptions = {
        "Brier score is mean (p - y)^2; logarithmic score is mean -[y log(p) + (1-y) log(1-p)] without clipping.",
        "Reliability bins are equal-width, lower-inclusive and upper-exclusive, except the final bin includes p=1; Wilson intervals are pointwise 95% binomial intervals.",
        "Sharpness describes the issued forecast distribution for all records, including those with missing outcomes: mean confidence/entropy, range, and equal-width probability-bin counts.",
        "Score intervals use a normal approximation to the standard error and are omitted for fewer than two observations or an infinite log score.",
        "Risk-coverage retains examples in descending max(p, 1-p), adds confidence ties together, predicts completion at p >= 0.5, and reports errors / retained.",
        "Subgroup names and shift flags are supplied by the caller; they are descriptive labels, not authenticated provenance or a validated shift detector.",
        "Conformal nonconformity is 1 - p(y); the finite-sample order statistic is ceil((n+1)(1-alpha))."};
    report.limitations = {
        "Partition labels and IDs are caller-supplied and are not authenticated; the checks enforce the declared manifest, not data provenance.",
        "Synthetic fixture summaries are software demonstrations, not benchmark evidence or estimates for a real task population.",
        "Reliability bins, subgroup counts, score uncertainty, and risk-coverage are descriptive and do not guarantee conditional reliability or utility.",
        "No weighted conformal method is implemented; no arbitrary distribution-shift guarantee is available.",
        "The Phase 0 WMC corpus has no outcome labels or calibration split; no corpus row or odd-indexed holdout record is consumed."};
    return report;
}

std::optional<BinaryPredictionSet> make_binary_prediction_set(
    long double completion_probability, const ConformalThreshold& threshold) {
    if (!std::isfinite(completion_probability) || completion_probability < 0.0L ||
        completion_probability > 1.0L || threshold.status != ConformalStatus::evaluated ||
        !threshold.nonconformity_threshold.has_value())
        return std::nullopt;
    if (threshold.threshold_unbounded)
        return BinaryPredictionSet{true, true};
    const auto value = *threshold.nonconformity_threshold;
    return BinaryPredictionSet{completion_probability <= value,
                               1.0L - completion_probability <= value};
}

}  // namespace xai::calibration
