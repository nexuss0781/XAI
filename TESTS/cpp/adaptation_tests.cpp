#include "xai/adaptation.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using xai::adaptation::Controller;
using xai::adaptation::ControllerOptions;
using xai::adaptation::ControllerState;
using xai::adaptation::Decision;
using xai::adaptation::DevelopmentCase;
using xai::adaptation::DevelopmentSuite;
using xai::adaptation::EventKind;
using xai::adaptation::StreamingObservation;
using xai::contracts::DataPartition;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

DevelopmentSuite single_clause_suite(const std::string& suite_id,
                                     std::string formula = "p cnf 2 1\n1 2 0\n",
                                     xai::wmc::Rational expected = 3) {
    std::istringstream input(formula);
    DevelopmentCase item;
    item.observation_id = xai::contracts::ObservationId{"dev-case-1"};
    item.partition = DataPartition::development;
    item.instance = xai::wmc::parse_dimacs_wmc(input);
    item.expected_count = std::move(expected);
    item.expected_satisfiable = true;
    return {suite_id, xai::contracts::PartitionId{"phase5-dev"},
            DataPartition::development, {std::move(item)}};
}

DevelopmentSuite harder_suite() {
    // Only the all-false assignment is excluded by the single five-literal
    // clause, so the exact unweighted count is 2^5 - 1 = 31. The search tree exceeds the
    // adapted 4-node cap but is within the pinned 128-node baseline.
    auto suite = single_clause_suite("dev-hard-v1", "p cnf 5 1\n1 2 3 4 5 0\n", 31);
    return suite;
}

ControllerOptions options_with_detector() {
    ControllerOptions options;
    options.fixed_solver_options.timeout_ms = 2'000;
    options.detector.mu_in_control = 0.0;
    options.detector.mu_shift = 0.2;
    options.detector.sigma = 0.1;
    options.detector.threshold = 1.0;
    options.detector.sample_cadence = 2;
    options.detector.hold_off_samples = 2;
    return options;
}

void test_finite_grid_and_partition_isolation() {
    const std::vector<std::uint64_t> expected_grid{1, 2, 4, 8, 16, 32, 64, 128};
    require(xai::adaptation::node_limit_grid() == expected_grid,
            "the mutable node cap must use the declared finite grid");

    auto options = options_with_detector();
    Controller controller(options);
    auto held_out = single_clause_suite("holdout-v1");
    held_out.partition = DataPartition::final_test;
    const auto rejected = controller.search(held_out);
    require(rejected.decision == Decision::rejected_partition,
            "final-test suite must be rejected");
    require(rejected.evaluated_node_limits.empty() && !rejected.baseline_completed,
            "held-out data must not be evaluated or scored");
    require(rejected.active_node_limit == xai::adaptation::kBaselineNodeLimit &&
                controller.state() == ControllerState::exploring,
            "partition rejection must leave the baseline and search state unchanged");

    auto mixed = single_clause_suite("mixed-dev-v1");
    mixed.cases.front().partition = DataPartition::final_test;
    const auto mixed_rejected = controller.search(mixed);
    require(mixed_rejected.decision == Decision::rejected_partition &&
                mixed_rejected.evaluated_node_limits.empty(),
            "mixed development/final-test records must be rejected before evaluation");

    auto duplicates = single_clause_suite("dev-duplicates-v1");
    duplicates.cases.push_back(duplicates.cases.front());
    const auto duplicate_rejected = controller.search(duplicates);
    require(duplicate_rejected.decision == Decision::invalid_input &&
                duplicate_rejected.evaluated_node_limits.empty(),
            "duplicate observation IDs must not silently reweight the objective");

    const auto result = controller.search(single_clause_suite("dev-small-v1"));
    require(result.decision == Decision::updated,
            "development suite should select a smaller adequate node cap");
    require(result.baseline_completed == 1 && result.selected_completed == 1,
            "search must retain exact completion on the development suite");
    require(result.active_node_limit == 4,
            "the smallest cap that completes the one-clause fixture should be selected");
    require(result.evaluated_node_limits == expected_grid,
            "search must evaluate the deterministic finite candidate grid");
    require(result.candidate_scores.size() == expected_grid.size() &&
                controller.audit_log().back().candidate_scores == result.candidate_scores,
            "every candidate objective and resource count must be retained in the audit");
    for (std::size_t index = 0; index < result.candidate_scores.size(); ++index) {
        require(result.candidate_scores[index].node_limit == expected_grid[index] &&
                    result.candidate_scores[index].total_cases == 1,
                "candidate audit must name each allowed bound and its denominator");
        if (index > 0) {
            require(result.candidate_scores[index - 1].completed <=
                        result.candidate_scores[index].completed,
                    "larger node caps must not reduce exact completion on the same fixed suite");
        }
    }
    require(controller.active_version() != options.baseline_version,
            "accepted changes must receive a distinct version from baseline");
    require(controller.audit_log().size() == 3,
            "partition rejection and accepted search transitions must be auditable");
    require(controller.audit_log().back().kind == EventKind::search_updated &&
                controller.audit_log().back().partition == DataPartition::development,
            "accepted candidate history must record its development partition");
}

void test_deterministic_replay() {
    const auto suite = single_clause_suite("dev-replay-v1");
    Controller first(options_with_detector());
    Controller second(options_with_detector());
    const auto a = first.search(suite);
    const auto b = second.search(suite);
    require(a == b, "identical search input and initial state must replay identically");
    require(first.audit_log() == second.audit_log(),
            "version transitions and audit records must replay identically");

    const auto unchanged_a = first.search(suite);
    const auto unchanged_b = second.search(suite);
    require(unchanged_a == unchanged_b && first.audit_log() == second.audit_log(),
            "repeated optimization must preserve deterministic state and history");
    require(unchanged_a.decision == Decision::unchanged &&
                unchanged_a.active_node_limit == 4,
            "stable development data must retain the accepted configuration");
}

void test_regression_rollback_and_recovery() {
    Controller controller(options_with_detector());
    const auto accepted = controller.search(single_clause_suite("dev-easy-v1"));
    require(accepted.active_node_limit == 4, "fixture must first accept the adapted cap");

    const auto regressed = controller.search(harder_suite());
    require(regressed.decision == Decision::alarm_frozen &&
                regressed.state == ControllerState::frozen,
            "an adapted setting that loses development completions must freeze");
    require(regressed.active_node_limit == xai::adaptation::kBaselineNodeLimit &&
                regressed.active_version == options_with_detector().baseline_version,
            "objective regression must restore the exact versioned baseline");
    require(controller.audit_log().back().kind == EventKind::regression_rollback,
            "regression rollback must record an explicit reason");
    require(controller.reset() == Decision::reset_blocked,
            "operator reset must remain blocked during hold-off");

    StreamingObservation cooldown_one{xai::contracts::ObservationId{"stream-cool-1"},
        xai::contracts::PartitionId{"runtime-stream"}, DataPartition::streaming, true, 1};
    StreamingObservation cooldown_two{xai::contracts::ObservationId{"stream-cool-2"},
        xai::contracts::PartitionId{"runtime-stream"}, DataPartition::streaming, true, 1};
    (void)controller.observe(cooldown_one);
    require(controller.hold_off_remaining() == 1, "first hold-off observation must be counted");
    (void)controller.observe(cooldown_two);
    require(controller.hold_off_remaining() == 0, "hold-off must end after its declared sample count");
    require(controller.reset() == Decision::reset &&
                controller.state() == ControllerState::exploring &&
                controller.active_node_limit() == xai::adaptation::kBaselineNodeLimit,
            "explicit reset must reopen exploration from baseline only");

    const auto recovered = controller.search(single_clause_suite("dev-recovery-v1"));
    require(recovered.decision == Decision::updated && recovered.active_node_limit == 4,
            "controller must recover by re-evaluating from baseline after reset");
    require(controller.audit_log().back().kind == EventKind::search_updated,
            "post-reset adaptation must append a new versioned transition");
}

void test_shift_alarm_cadence_rollback_and_reset() {
    Controller controller(options_with_detector());
    const auto accepted = controller.search(single_clause_suite("dev-monitor-v1"));
    require(accepted.active_node_limit == 4, "monitor fixture must begin with an adapted version");

    StreamingObservation first{xai::contracts::ObservationId{"stream-1"},
        xai::contracts::PartitionId{"runtime-stream"}, DataPartition::streaming, true, 4};
    const auto first_result = controller.observe(first);
    require(first_result.decision == Decision::unchanged && !first_result.sampled,
            "CUSUM must respect the declared two-sample cadence");

    StreamingObservation second{xai::contracts::ObservationId{"stream-2"},
        xai::contracts::PartitionId{"runtime-stream"}, DataPartition::streaming, true, 4};
    const auto alarm = controller.observe(second);
    require(alarm.decision == Decision::alarm_frozen && alarm.sampled && alarm.residual,
            "shifted residual must cross the configured Gaussian CUSUM threshold");
    require(alarm.state == ControllerState::frozen &&
                alarm.active_node_limit == xai::adaptation::kBaselineNodeLimit,
            "shift alarm must freeze exploration and restore baseline immediately");
    require(controller.audit_log().back().kind == EventKind::shift_alarm_rollback,
            "shift alarm must retain residual, CUSUM, and rollback transition");

    StreamingObservation final_record{xai::contracts::ObservationId{"final-1"},
        xai::contracts::PartitionId{"locked-final"}, DataPartition::final_test, true, 4};
    const auto remaining_before = controller.hold_off_remaining();
    const auto final_rejected = controller.observe(final_record);
    require(final_rejected.decision == Decision::rejected_partition &&
                controller.hold_off_remaining() == remaining_before,
            "final-test residuals must neither reach CUSUM nor consume hold-off");
    require(controller.reset() == Decision::reset_blocked,
            "alarm reset must await all hold-off samples");

    StreamingObservation cooldown_one{xai::contracts::ObservationId{"stream-hold-1"},
        xai::contracts::PartitionId{"runtime-stream"}, DataPartition::streaming, true, 1};
    StreamingObservation cooldown_two{xai::contracts::ObservationId{"stream-hold-2"},
        xai::contracts::PartitionId{"runtime-stream"}, DataPartition::streaming, true, 1};
    (void)controller.observe(cooldown_one);
    (void)controller.observe(cooldown_two);
    require(controller.reset() == Decision::reset && controller.cusum() == 0.0,
            "operator reset must clear detector state after hold-off");
    require(controller.active_node_limit() == xai::adaptation::kBaselineNodeLimit,
            "detector reset must never resume directly on a non-baseline candidate");
}

void test_invariant_failure_and_invalid_configuration() {
    Controller mismatch_controller(options_with_detector());
    const auto mismatch = mismatch_controller.search(
        single_clause_suite("dev-bad-oracle-v1", "p cnf 2 1\n1 2 0\n", 2));
    require(mismatch.decision == Decision::alarm_frozen &&
                mismatch.active_node_limit == xai::adaptation::kBaselineNodeLimit,
            "an exact development-oracle mismatch must fail closed to baseline");
    require(mismatch_controller.audit_log().back().kind == EventKind::invariant_rollback,
            "invariant failure must be machine-distinguishable in the audit log");

    Controller failure_controller(options_with_detector());
    (void)failure_controller.search(single_clause_suite("dev-failure-v1"));
    StreamingObservation failed{xai::contracts::ObservationId{"stream-failure"},
        xai::contracts::PartitionId{"runtime-stream"}, DataPartition::streaming, false, 0};
    const auto failed_result = failure_controller.observe(failed);
    require(failed_result.decision == Decision::alarm_frozen &&
                failed_result.active_node_limit == xai::adaptation::kBaselineNodeLimit,
            "streaming solver failure must immediately restore baseline and freeze");
    require(failure_controller.audit_log().back().kind == EventKind::invariant_rollback,
            "streaming failure must have explicit invariant-failure evidence");

    auto invalid_options = options_with_detector();
    invalid_options.detector.sigma = 0.0;
    bool rejected = false;
    try {
        Controller invalid(invalid_options);
        (void)invalid;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "degenerate detector distributions must be rejected at construction");
}

}  // namespace

int main() {
    try {
        test_finite_grid_and_partition_isolation();
        std::cout << "PASS bounded_search: finite node-cap grid, exact completion objective, final-test isolation\n";
        test_deterministic_replay();
        std::cout << "PASS replay: deterministic candidate ranking, versions, and audit history\n";
        test_regression_rollback_and_recovery();
        std::cout << "PASS regression: baseline rollback, explicit hold-off, reset, and recovery\n";
        test_shift_alarm_cadence_rollback_and_reset();
        std::cout << "PASS change_monitor: residual cadence, Gaussian CUSUM alarm, baseline freeze, hold-off\n";
        test_invariant_failure_and_invalid_configuration();
        std::cout << "PASS fail_closed: oracle and stream failures roll back; invalid detector is rejected\n";
        std::cout << "RESULT: 5/5 Phase 5 adaptation groups passed. Evidence is finite software validation only.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
