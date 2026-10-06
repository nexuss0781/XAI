#pragma once

#include "xai/contracts.hpp"
#include "xai/wmc.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xai::adaptation {

// This phase adapts exactly one WMC setting. Every candidate is selected from
// node_limit_grid(); no other SolverOptions field is mutable here.
inline constexpr std::uint64_t kBaselineNodeLimit = 128;
inline constexpr std::uint64_t kMaximumNodeLimit = 128;

[[nodiscard]] const std::vector<std::uint64_t>& node_limit_grid();

enum class ControllerState { exploring, frozen };
enum class Decision {
    updated,
    unchanged,
    rejected_partition,
    invalid_input,
    alarm_frozen,
    reset,
    reset_blocked
};
enum class EventKind {
    search_updated,
    search_unchanged,
    partition_rejected,
    residual_observed,
    regression_rollback,
    invariant_rollback,
    shift_alarm_rollback,
    reset
};

struct CandidateScore {
    std::uint64_t node_limit{0};
    std::size_t completed{0};
    std::size_t total_cases{0};
    std::uint64_t total_recursive_nodes{0};
    double mean_log_nodes{0.0};
    friend bool operator==(const CandidateScore&, const CandidateScore&) = default;
};

struct DevelopmentCase {
    contracts::ObservationId observation_id;
    contracts::DataPartition partition{contracts::DataPartition::development};
    wmc::Instance instance;
    wmc::Rational expected_count{0};
    bool expected_satisfiable{false};
};

struct DevelopmentSuite {
    std::string suite_id;
    contracts::PartitionId partition_id;
    contracts::DataPartition partition{contracts::DataPartition::development};
    std::vector<DevelopmentCase> cases;
};

struct DetectorConfig {
    // Residual: log(1 + observed recursive nodes) minus the mean baseline
    // log(1 + recursive nodes) measured on the first accepted development suite.
    // CUSUM assumes Normal(mu_in_control, sigma^2) versus
    // Normal(mu_shift, sigma^2), with mu_shift > mu_in_control.
    double mu_in_control{0.0};
    double mu_shift{0.5};
    double sigma{0.25};
    double threshold{5.0};
    std::uint64_t sample_cadence{1};
    std::uint64_t hold_off_samples{3};
};

struct ControllerOptions {
    // All fixed fields are held constant across candidates. node_limit is
    // overwritten by the finite adaptation grid for every evaluation.
    wmc::SolverOptions fixed_solver_options{};
    std::size_t max_development_cases{256};
    DetectorConfig detector{};
    std::string baseline_version{"wmc-node-limit-baseline-v1"};
};

struct SearchResult {
    Decision decision{Decision::invalid_input};
    ControllerState state{ControllerState::exploring};
    std::uint64_t active_node_limit{kBaselineNodeLimit};
    std::string active_version;
    std::optional<std::size_t> baseline_completed;
    std::optional<std::size_t> selected_completed;
    std::vector<std::uint64_t> evaluated_node_limits;
    std::string diagnostic;
    std::vector<CandidateScore> candidate_scores;
    friend bool operator==(const SearchResult&, const SearchResult&) = default;
};

struct StreamingObservation {
    contracts::ObservationId observation_id;
    contracts::PartitionId partition_id;
    contracts::DataPartition partition{contracts::DataPartition::streaming};
    bool solver_success{true};
    std::uint64_t recursive_nodes{0};
};

struct MonitorResult {
    Decision decision{Decision::invalid_input};
    ControllerState state{ControllerState::exploring};
    bool sampled{false};
    std::optional<double> residual;
    double cusum{0.0};
    std::uint64_t hold_off_remaining{0};
    std::uint64_t active_node_limit{kBaselineNodeLimit};
    std::string active_version;
    std::string diagnostic;
    friend bool operator==(const MonitorResult&, const MonitorResult&) = default;
};

struct AuditEvent {
    std::uint64_t sequence{0};
    EventKind kind{EventKind::search_unchanged};
    contracts::DataPartition partition{contracts::DataPartition::not_applicable};
    std::string suite_or_observation_id;
    std::string from_version;
    std::string to_version;
    std::optional<std::size_t> baseline_completed;
    std::optional<std::size_t> selected_completed;
    std::optional<double> residual;
    std::optional<double> cusum;
    std::string detail;
    std::vector<CandidateScore> candidate_scores;
    friend bool operator==(const AuditEvent&, const AuditEvent&) = default;
};

class Controller {
public:
    explicit Controller(ControllerOptions options = {});

    // Accepts development-labeled records only. The entire suite is preflighted
    // before any formula is evaluated; final-test data are rejected without
    // reading case payloads.
    [[nodiscard]] SearchResult search(const DevelopmentSuite& suite);

    // Monitors streaming-only solver outcomes. A final-test record is rejected
    // before its success flag or resource count is read.
    [[nodiscard]] MonitorResult observe(const StreamingObservation& observation);

    // Exploration can resume only after the declared hold-off observations
    // have elapsed and an explicit reset is requested. Reset always starts from
    // the versioned baseline.
    [[nodiscard]] Decision reset();

    [[nodiscard]] ControllerState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t active_node_limit() const noexcept { return active_node_limit_; }
    [[nodiscard]] const std::string& active_version() const noexcept { return active_version_; }
    [[nodiscard]] double cusum() const noexcept { return cusum_; }
    [[nodiscard]] std::uint64_t hold_off_remaining() const noexcept {
        return hold_off_remaining_;
    }
    [[nodiscard]] const std::vector<AuditEvent>& audit_log() const noexcept {
        return audit_log_;
    }

private:
    struct Evaluation {
        std::size_t completed{0};
        std::uint64_t total_recursive_nodes{0};
        double mean_log_nodes{0.0};
    };

    ControllerOptions options_;
    ControllerState state_{ControllerState::exploring};
    std::uint64_t active_node_limit_{kBaselineNodeLimit};
    std::string active_version_;
    std::uint64_t version_counter_{0};
    std::uint64_t accepted_stream_samples_{0};
    std::uint64_t hold_off_remaining_{0};
    double cusum_{0.0};
    std::optional<double> residual_reference_;
    std::vector<AuditEvent> audit_log_;

    [[nodiscard]] Evaluation evaluate(const DevelopmentSuite& suite,
                                      std::uint64_t node_limit) const;
    void freeze(EventKind kind, contracts::DataPartition partition,
                std::string source_id, std::string detail,
                std::optional<std::size_t> baseline_completed = std::nullopt,
                std::optional<std::size_t> selected_completed = std::nullopt,
                std::optional<double> residual = std::nullopt,
                std::optional<double> cusum = std::nullopt);
    void add_event(AuditEvent event);
    [[nodiscard]] SearchResult current_result(Decision decision,
                                              std::string diagnostic) const;
    [[nodiscard]] MonitorResult current_monitor_result(Decision decision,
                                                        bool sampled,
                                                        std::optional<double> residual,
                                                        std::string diagnostic) const;
};

}  // namespace xai::adaptation
