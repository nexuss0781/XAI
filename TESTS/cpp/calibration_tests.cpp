#include "xai/calibration.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace xai::contracts;
using namespace xai::calibration;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void near(long double actual, long double expected, long double tolerance,
          const std::string& message) {
    if (std::abs(actual - expected) > tolerance) throw std::runtime_error(message);
}

CalibrationProtocol protocol_for(const std::vector<std::string>& ids,
                                 bool exchangeability = false) {
    CalibrationProtocol protocol;
    protocol.protocol_id = "phase6-synthetic-protocol-v1";
    protocol.dataset_id = "phase6-synthetic-completion-fixture";
    protocol.dataset_version = "fixture-2026-10-07-v1";
    protocol.selection_policy = "fixed hand-authored synthetic forecast/outcome fixture; no corpus rows; not exchangeability-validated";
    protocol.evidence_kind = EvidenceKind::synthetic_fixture;
    protocol.calibration_observation_ids = ids;
    protocol.final_test_observation_ids = {"obs:synthetic-final-01", "obs:synthetic-final-02"};
    protocol.reliability_bin_count = 5;
    protocol.minimum_subgroup_size = 2;
    protocol.conformal_alpha = 0.2L;
    protocol.exchangeability_defensible = exchangeability;
    if (exchangeability)
        protocol.exchangeability_basis = "test-only stipulated exchangeability assumption for order-statistic arithmetic; not an empirical or benchmark claim";
    return protocol;
}

Forecast row(const std::string& id, long double p, std::optional<bool> outcome,
             const std::string& group = "", bool shifted = false) {
    return {ObservationId{"obs:" + id}, PartitionId{"partition:phase6-calibration-v1"},
            DataPartition::calibration, p, outcome, group, shifted};
}

std::vector<Forecast> demo_fixture() {
    return {
        row("cal-01", 0.95L, true, "small"), row("cal-02", 0.85L, true, "small"),
        row("cal-03", 0.75L, false, "large", true), row("cal-04", 0.65L, true, "large"),
        row("cal-05", 0.55L, true, "small"), row("cal-06", 0.45L, false, "large"),
        row("cal-07", 0.35L, false, "small"), row("cal-08", 0.25L, false, "large"),
        row("cal-09", 0.15L, true, "small", true), row("cal-10", 0.05L, false, "large"),
        row("cal-11", 0.90L, true, "small"), row("cal-12", 0.70L, false, "large"),
        row("cal-13", 0.30L, false, "small"), row("cal-14", 0.10L, true, "large"),
        row("cal-15", 0.60L, std::nullopt, "small"), row("cal-16", 0.40L, false, "large")};
}

CalibrationReport evaluate_demo() {
    const auto forecasts = demo_fixture();
    std::vector<std::string> ids;
    ids.reserve(forecasts.size());
    for (const auto& item : forecasts) ids.push_back(item.observation_id.value());
    return evaluate(protocol_for(ids, false), forecasts);
}

void test_proper_scores_and_infinite_log_loss() {
    auto forecasts = std::vector<Forecast>{row("score-01", 0.8L, true), row("score-02", 0.2L, false)};
    auto protocol = protocol_for({"obs:score-01", "obs:score-02"});
    protocol.reliability_bin_count = 2;
    const auto report = evaluate(protocol, forecasts);
    require(report.status == Status::success && report.scored_count == 2,
            "valid score fixture did not return success");
    require(report.brier_score && report.logarithmic_score, "proper score outputs are missing");
    near(report.brier_score->mean, 0.04L, 1e-15L, "Brier score definition is wrong");
    near(report.logarithmic_score->mean, -std::log(0.8L), 1e-15L, "log score definition is wrong");
    require(report.brier_score->normal_approx_95_interval.has_value(),
            "score uncertainty was not reported for a sample of size two");

    auto endpoint_protocol = protocol_for({"obs:endpoint"});
    const auto endpoint = evaluate(endpoint_protocol, {row("endpoint", 0.0L, true)});
    require(endpoint.logarithmic_score && std::isinf(endpoint.logarithmic_score->mean) &&
                !endpoint.logarithmic_score->normal_approx_95_interval,
            "unclipped log score did not preserve infinite loss for a confident wrong forecast");
}

void test_reliability_boundaries_and_wilson_intervals() {
    auto forecasts = std::vector<Forecast>{row("edge-00", 0.0L, false),
        row("edge-50", 0.5L, true), row("edge-100", 1.0L, true)};
    auto protocol = protocol_for({"obs:edge-00", "obs:edge-50", "obs:edge-100"});
    protocol.reliability_bin_count = 2;
    const auto report = evaluate(protocol, forecasts);
    require(report.reliability_bins.size() == 2, "declared reliability bins were not preserved");
    require(report.reliability_bins[0].count == 1 && report.reliability_bins[1].count == 2,
            "bin edge convention is incorrect at p=0.5 or p=1");
    require(report.reliability_bins[0].wilson_95_interval.has_value() &&
                report.reliability_bins[1].wilson_95_interval.has_value(),
            "non-empty bins lack Wilson uncertainty intervals");
    require(report.reliability_bins[1].upper_edge_inclusive,
            "the final reliability bin must include probability one");
}

void test_risk_coverage_ties_and_outcome_accounting() {
    auto forecasts = std::vector<Forecast>{row("risk-a", 0.9L, true), row("risk-b", 0.1L, false),
        row("risk-c", 0.8L, false), row("risk-d", 0.2L, true), row("risk-missing", 0.7L, std::nullopt)};
    const auto protocol = protocol_for({"obs:risk-a", "obs:risk-b", "obs:risk-c", "obs:risk-d", "obs:risk-missing"});
    const auto report = evaluate(protocol, forecasts);
    require(report.observation_count == 5 && report.scored_count == 4 && report.missing_outcome_count == 1,
            "sample counts do not separate missing outcomes");
    require(report.risk_coverage.size() == 2, "equal-confidence cases should be retained as two tie groups");
    require(report.risk_coverage[0].retained_count == 2 && report.risk_coverage[0].coverage == 0.5L,
            "the highest-confidence tie group was not retained atomically");
    near(report.risk_coverage[0].selective_risk, 0.0L, 1e-15L,
         "risk-coverage error rate for the first retained group is wrong");
    require(report.subgroups.at("unspecified").missing_outcome_count == 1,
            "missing subgroup name was not accounted for explicitly");
}

void test_conformal_order_statistic_and_unbounded_boundary() {
    auto finite_forecasts = std::vector<Forecast>{
        row("conf-a", 0.9L, true), row("conf-b", 0.8L, false), row("conf-c", 0.7L, true),
        row("conf-d", 0.6L, true), row("conf-e", 0.4L, false), row("conf-f", 0.3L, false),
        row("conf-g", 0.2L, true), row("conf-h", 0.1L, false), row("conf-i", 0.55L, true)};
    std::vector<std::string> finite_ids;
    for (const auto& item : finite_forecasts) finite_ids.push_back(item.observation_id.value());
    auto finite_protocol = protocol_for(finite_ids, true);
    finite_protocol.conformal_alpha = 0.2L;
    const auto finite = evaluate(finite_protocol, finite_forecasts).conformal;
    require(finite.status == ConformalStatus::evaluated && finite.order_statistic_index == 8 &&
                !finite.threshold_unbounded && finite.nonconformity_threshold,
            "finite-sample conformal order statistic is wrong");
    const auto finite_set = make_binary_prediction_set(0.95L, finite);
    require(finite_set.has_value(), "authorized conformal set was not returned");

    auto small_protocol = protocol_for({"obs:small-a", "obs:small-b"}, true);
    small_protocol.conformal_alpha = 0.1L;
    const auto unbounded = evaluate(small_protocol,
        {row("small-a", 0.7L, true), row("small-b", 0.2L, false)}).conformal;
    require(unbounded.status == ConformalStatus::evaluated && unbounded.order_statistic_index == 3 &&
                unbounded.threshold_unbounded && std::isinf(*unbounded.nonconformity_threshold),
            "k > n must produce the unbounded conformal score threshold");
    const auto full_set = make_binary_prediction_set(0.37L, unbounded);
    require(full_set && full_set->include_noncompletion && full_set->include_completion,
            "k > n must return the full finite binary label set");
}

void test_unsupported_conformal_assumptions_and_partition_rejection() {
    const auto unsupported = evaluate(protocol_for({"obs:unsupported"}),
        {row("unsupported", 0.6L, true)}).conformal;
    require(unsupported.status == ConformalStatus::unsupported_assumption &&
                !unsupported.conditional_coverage_statement &&
                !make_binary_prediction_set(0.6L, unsupported),
            "unsupported exchangeability emitted a conformal result");

    auto mixed = row("mixed", 0.6L, true);
    mixed.partition = DataPartition::final_test;
    bool final_test_rejected = false;
    try { (void)evaluate(protocol_for({"obs:mixed"}), {mixed}); }
    catch (const std::invalid_argument&) { final_test_rejected = true; }
    require(final_test_rejected, "final-test-labeled record passed the calibration gate");

    auto mismatched_partition = row("mismatch", 0.6L, true);
    mismatched_partition.partition_id = PartitionId{"partition:other"};
    bool partition_id_rejected = false;
    try { (void)evaluate(protocol_for({"obs:mismatch"}), {mismatched_partition}); }
    catch (const std::invalid_argument&) { partition_id_rejected = true; }
    require(partition_id_rejected, "mismatched partition ID passed the calibration gate");
}

void test_manifest_disjointness_and_exact_calibration_membership() {
    auto overlap = protocol_for({"obs:overlap"});
    overlap.fitting_observation_ids.push_back("obs:overlap");
    bool overlap_rejected = false;
    try { (void)evaluate(overlap, {row("overlap", 0.6L, true)}); }
    catch (const std::invalid_argument&) { overlap_rejected = true; }
    require(overlap_rejected, "overlapping fitting and calibration IDs were not rejected");

    bool missing_rejected = false;
    try { (void)evaluate(protocol_for({"obs:expected"}), {row("unexpected", 0.6L, true)}); }
    catch (const std::invalid_argument&) { missing_rejected = true; }
    require(missing_rejected, "calibration records outside the frozen ID manifest were accepted");
}

void test_subgroup_shift_and_empty_outcomes() {
    auto protocol = protocol_for({"obs:sub-1", "obs:sub-2", "obs:sub-3"});
    protocol.minimum_subgroup_size = 2;
    const auto report = evaluate(protocol, {row("sub-1", 0.8L, true, "rare", true),
        row("sub-2", 0.2L, std::nullopt, "rare"), row("sub-3", 0.6L, false, "common")});
    require(report.shift_flagged_count == 1 && report.shift_flagged_scored_count == 1,
            "shift flags were not counted separately");
    require(report.subgroups.at("rare").total_count == 2 && report.subgroups.at("rare").scored_count == 1 &&
                report.subgroups.at("rare").below_minimum_sample_size,
            "subgroup counts or small-sample warning are wrong");
    require(report.subgroups.at("common").below_minimum_sample_size,
            "small subgroup was not marked as sample-limited");

    const auto empty = evaluate(protocol_for({"obs:none-a", "obs:none-b"}),
        {row("none-a", 0.6L, std::nullopt), row("none-b", 0.4L, std::nullopt)});
    require(empty.status == Status::unknown && empty.scored_count == 0 && !empty.brier_score &&
                empty.conformal.status == ConformalStatus::no_outcomes,
            "missing-outcome-only sample did not fail explicitly without fabricated metrics");
}

void test_probability_and_sample_limits() {
    auto invalid_probability = row("bad-p", std::numeric_limits<long double>::quiet_NaN(), true);
    bool probability_rejected = false;
    try { (void)evaluate(protocol_for({"obs:bad-p"}), {invalid_probability}); }
    catch (const std::invalid_argument&) { probability_rejected = true; }
    require(probability_rejected, "non-finite probability was accepted");

    auto limited = protocol_for({"obs:limit-a", "obs:limit-b"});
    limited.max_observations = 1;
    bool limit_rejected = false;
    try { (void)evaluate(limited, {row("limit-a", 0.5L, true), row("limit-b", 0.5L, false)}); }
    catch (const std::invalid_argument&) { limit_rejected = true; }
    require(limit_rejected, "configured calibration observation limit was ignored");

    auto label_protocol = protocol_for({"obs:long-label"});
    label_protocol.max_subgroup_label_bytes = 4;
    auto long_label = row("long-label", 0.5L, true, "oversized");
    bool label_rejected = false;
    try { (void)evaluate(label_protocol, {long_label}); }
    catch (const std::invalid_argument&) { label_rejected = true; }
    require(label_rejected, "subgroup label byte limit was ignored");
}

void test_synthetic_report_separates_diagnostics() {
    const auto report = evaluate_demo();
    require(report.evidence_kind == "synthetic_fixture" && report.dataset_version == "fixture-2026-10-07-v1",
            "synthetic fixture identity/version was not propagated");
    require(report.observation_count == 16 && report.scored_count == 15 && report.missing_outcome_count == 1,
            "frozen synthetic fixture sample accounting is wrong");
    require(report.sharpness.forecast_count == report.observation_count &&
                report.sharpness.probability_bin_counts.size() == report.reliability_bins.size() &&
                report.sharpness.mean_predictive_entropy_nats >= 0.0L &&
                report.sharpness.mean_confidence >= 0.5L,
            "sharpness summary must be separate and include all issued forecasts");
    require(report.brier_score && report.logarithmic_score && !report.reliability_bins.empty() &&
                !report.risk_coverage.empty() && !report.subgroups.empty(),
            "one or more distinct calibration diagnostics are missing");
    require(report.conformal.status == ConformalStatus::unsupported_assumption &&
                !report.conformal.conditional_coverage_statement &&
                report.conformal.limitation.find("no coverage statement") != std::string::npos,
            "hand-authored fixture improperly emitted a conformal coverage statement");
    std::cout << std::fixed << std::setprecision(6)
              << "synthetic_fixture dataset=" << report.dataset_id
              << " version=" << report.dataset_version
              << " model=" << report.model_id
              << " observations=" << report.observation_count
              << " scored=" << report.scored_count << " missing=" << report.missing_outcome_count
              << " brier=" << static_cast<double>(report.brier_score->mean)
              << " brier_se=" << static_cast<double>(report.brier_score->standard_error.value_or(0.0L))
              << " log_score=" << static_cast<double>(report.logarithmic_score->mean)
              << " log_score_se=" << static_cast<double>(report.logarithmic_score->standard_error.value_or(0.0L))
              << " sharpness_mean_p=" << static_cast<double>(report.sharpness.mean_completion_probability)
              << " sharpness_mean_confidence=" << static_cast<double>(report.sharpness.mean_confidence)
              << " sharpness_entropy_nats=" << static_cast<double>(report.sharpness.mean_predictive_entropy_nats)
              << " reliability_bins=" << report.reliability_bins.size()
              << " risk_coverage_points=" << report.risk_coverage.size()
              << " shift_flags=" << report.shift_flagged_count
              << " conformal_n=" << report.conformal.calibration_sample_count
              << " conformal_status=unsupported_assumption"
              << " empirical_coverage=not_evaluated\n";
    for (std::size_t index = 0; index < report.reliability_bins.size(); ++index) {
        const auto& bin = report.reliability_bins[index];
        std::cout << "reliability_bin=" << index << " range=["
                  << static_cast<double>(bin.lower_edge) << ','
                  << static_cast<double>(bin.upper_edge) << (bin.upper_edge_inclusive ? "]" : ")")
                  << " n=" << bin.count;
        if (bin.mean_forecast && bin.observed_rate)
            std::cout << " mean_p=" << static_cast<double>(*bin.mean_forecast)
                      << " event_rate=" << static_cast<double>(*bin.observed_rate);
        if (bin.wilson_95_interval)
            std::cout << " wilson95=[" << static_cast<double>(bin.wilson_95_interval->lower)
                      << ',' << static_cast<double>(bin.wilson_95_interval->upper) << ']';
        std::cout << '\n';
    }
}
}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"proper scores and infinite log loss", test_proper_scores_and_infinite_log_loss},
        {"reliability boundaries and Wilson intervals", test_reliability_boundaries_and_wilson_intervals},
        {"risk-coverage ties and outcome accounting", test_risk_coverage_ties_and_outcome_accounting},
        {"conformal order statistic and unbounded boundary", test_conformal_order_statistic_and_unbounded_boundary},
        {"unsupported assumptions and partition rejection", test_unsupported_conformal_assumptions_and_partition_rejection},
        {"manifest disjointness and exact calibration membership", test_manifest_disjointness_and_exact_calibration_membership},
        {"subgroup, shift, and empty outcomes", test_subgroup_shift_and_empty_outcomes},
        {"probability and sample limits", test_probability_and_sample_limits},
        {"synthetic report and diagnostic separation", test_synthetic_report_separates_diagnostics}};
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    std::cout << "calibration tests passed: " << passed << '/' << tests.size() << '\n';
    return EXIT_SUCCESS;
}
