#include "xai/adaptation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <unordered_set>
#include <stdexcept>
#include <utility>

namespace xai::adaptation {
namespace {

constexpr std::array<std::uint64_t, 8> kNodeLimits{1, 2, 4, 8, 16, 32, 64, 128};

[[nodiscard]] bool valid_detector(const DetectorConfig& config) {
    const double delta = config.mu_shift - config.mu_in_control;
    const double midpoint = (config.mu_shift + config.mu_in_control) / 2.0;
    const double variance = config.sigma * config.sigma;
    return std::isfinite(config.mu_in_control) && std::isfinite(config.mu_shift) &&
           std::isfinite(config.sigma) && std::isfinite(config.threshold) &&
           std::isfinite(delta) && std::isfinite(midpoint) && std::isfinite(variance) &&
           config.mu_shift > config.mu_in_control && config.sigma > 0.0 && variance > 0.0 &&
           config.threshold > 0.0 && config.sample_cadence > 0 &&
           config.hold_off_samples > 0;
}

[[nodiscard]] double gaussian_log_likelihood_ratio(double residual,
                                                    const DetectorConfig& config) {
    const double delta = config.mu_shift - config.mu_in_control;
    const double midpoint = (config.mu_shift + config.mu_in_control) / 2.0;
    return delta * (residual - midpoint) / (config.sigma * config.sigma);
}

}  // namespace

const std::vector<std::uint64_t>& node_limit_grid() {
    static const std::vector<std::uint64_t> values(kNodeLimits.begin(), kNodeLimits.end());
    return values;
}

Controller::Controller(ControllerOptions options)
    : options_(std::move(options)), active_version_(options_.baseline_version) {
    if (options_.max_development_cases == 0 ||
        options_.max_development_cases > 100'000) {
        throw std::invalid_argument("development case limit must be in [1, 100000]");
    }
    if (!contracts::valid_identifier(options_.baseline_version)) {
        throw std::invalid_argument("baseline version must be a valid identifier");
    }
    if (!valid_detector(options_.detector)) {
        throw std::invalid_argument("invalid one-sided Gaussian CUSUM configuration");
    }
}

Controller::Evaluation Controller::evaluate(const DevelopmentSuite& suite,
                                            std::uint64_t node_limit) const {
    Evaluation result;
    long double log_node_sum = 0.0L;
    for (const auto& item : suite.cases) {
        auto solver_options = options_.fixed_solver_options;
        solver_options.node_limit = node_limit;
        try {
            const auto solved = wmc::exact_wmc(item.instance, solver_options);
            if (solved.count != item.expected_count ||
                solved.satisfiable != item.expected_satisfiable) {
                throw std::runtime_error("development oracle mismatch for observation " +
                                         item.observation_id.value());
            }
            ++result.completed;
            if (solved.stats.recursive_nodes >
                std::numeric_limits<std::uint64_t>::max() - result.total_recursive_nodes) {
                throw std::runtime_error("recursive-node accounting overflow");
            }
            result.total_recursive_nodes += solved.stats.recursive_nodes;
            log_node_sum += std::log1p(static_cast<long double>(solved.stats.recursive_nodes));
        } catch (const wmc::ResourceLimit&) {
            // A bounded noncompletion is an objective outcome, not a partial count.
        }
    }
    if (result.completed != 0) {
        result.mean_log_nodes = static_cast<double>(log_node_sum /
            static_cast<long double>(result.completed));
        if (!std::isfinite(result.mean_log_nodes)) {
            throw std::runtime_error("non-finite development residual reference");
        }
    }
    return result;
}

void Controller::add_event(AuditEvent event) {
    event.sequence = static_cast<std::uint64_t>(audit_log_.size()) + 1;
    audit_log_.push_back(std::move(event));
}

void Controller::freeze(EventKind kind, contracts::DataPartition partition,
                       std::string source_id, std::string detail,
                       std::optional<std::size_t> baseline_completed,
                       std::optional<std::size_t> selected_completed,
                       std::optional<double> residual,
                       std::optional<double> cusum) {
    const auto previous = active_version_;
    state_ = ControllerState::frozen;
    active_node_limit_ = kBaselineNodeLimit;
    active_version_ = options_.baseline_version;
    hold_off_remaining_ = options_.detector.hold_off_samples;
    const bool monitoring_event = kind == EventKind::shift_alarm_rollback;
    if (monitoring_event) cusum_ = cusum.value_or(cusum_);
    add_event({0, kind, partition, std::move(source_id), previous, active_version_,
               baseline_completed, selected_completed, residual, cusum,
               std::move(detail), {}});
}

SearchResult Controller::current_result(Decision decision,
                                         std::string diagnostic) const {
    return {decision, state_, active_node_limit_, active_version_, std::nullopt,
            std::nullopt, {}, std::move(diagnostic), {}};
}

MonitorResult Controller::current_monitor_result(Decision decision,
                                                   bool sampled,
                                                   std::optional<double> residual,
                                                   std::string diagnostic) const {
    return {decision, state_, sampled, residual, cusum_, hold_off_remaining_,
            active_node_limit_, active_version_, std::move(diagnostic)};
}

SearchResult Controller::search(const DevelopmentSuite& suite) {
    if (state_ == ControllerState::frozen) {
        return current_result(Decision::alarm_frozen, "exploration is frozen; baseline is active");
    }

    // Partition and shape preflight is intentionally completed before looking at
    // any formula or oracle value. No final-test payload is used for ranking.
    if (suite.partition != contracts::DataPartition::development) {
        add_event({0, EventKind::partition_rejected, suite.partition, {}, active_version_,
                   active_version_, std::nullopt, std::nullopt, std::nullopt,
                   std::nullopt, "adaptation objective requires development partition", {}});
        return current_result(Decision::rejected_partition,
                              "only the development partition may be optimized");
    }
    if (!contracts::valid_identifier(suite.suite_id) || !suite.partition_id.valid() ||
        suite.cases.empty() || suite.cases.size() > options_.max_development_cases) {
        return current_result(Decision::invalid_input,
                              "invalid suite ID, partition ID, or development case count");
    }
    std::unordered_set<std::string> observation_ids;
    observation_ids.reserve(suite.cases.size());
    for (const auto& item : suite.cases) {
        if (item.partition != contracts::DataPartition::development) {
            add_event({0, EventKind::partition_rejected, item.partition, {}, active_version_,
                       active_version_, std::nullopt, std::nullopt, std::nullopt,
                       std::nullopt, "every adaptation record must be development-labeled", {}});
            return current_result(Decision::rejected_partition,
                                  "mixed or non-development records are rejected before evaluation");
        }
        if (!item.observation_id.valid() || item.expected_count < 0 ||
            (item.expected_count > 0 && !item.expected_satisfiable) ||
            !observation_ids.insert(item.observation_id.value()).second) {
            return current_result(Decision::invalid_input,
                                  "invalid/duplicate observation ID or inconsistent exact oracle record");
        }
    }

    std::vector<std::pair<std::uint64_t, Evaluation>> evaluations;
    evaluations.reserve(kNodeLimits.size());
    try {
        for (const auto limit : kNodeLimits) {
            evaluations.emplace_back(limit, evaluate(suite, limit));
        }
    } catch (const std::exception& error) {
        freeze(EventKind::invariant_rollback, contracts::DataPartition::development,
               suite.suite_id, std::string("development evaluation failed: ") + error.what());
        return current_result(Decision::alarm_frozen,
                              "development invariant failed; the baseline was restored");
    }

    std::vector<CandidateScore> candidate_scores;
    candidate_scores.reserve(evaluations.size());
    for (const auto& [limit, evaluation] : evaluations) {
        candidate_scores.push_back({limit, evaluation.completed, suite.cases.size(),
                                    evaluation.total_recursive_nodes,
                                    evaluation.mean_log_nodes});
    }

    const auto find_evaluation = [&](std::uint64_t limit) -> const Evaluation& {
        const auto found = std::find_if(evaluations.begin(), evaluations.end(),
            [limit](const auto& item) { return item.first == limit; });
        if (found == evaluations.end()) throw std::logic_error("missing node-limit candidate");
        return found->second;
    };
    const auto& baseline = find_evaluation(kBaselineNodeLimit);
    if (baseline.completed == 0) {
        freeze(EventKind::invariant_rollback, contracts::DataPartition::development,
               suite.suite_id, "baseline completed no development cases");
        audit_log_.back().candidate_scores = candidate_scores;
        auto result = current_result(Decision::alarm_frozen,
                                     "baseline has no successful development reference; updates are frozen");
        result.candidate_scores = candidate_scores;
        return result;
    }
    if (!residual_reference_) residual_reference_ = baseline.mean_log_nodes;

    const auto& active = find_evaluation(active_node_limit_);
    if (active_node_limit_ != kBaselineNodeLimit && active.completed < baseline.completed) {
        freeze(EventKind::regression_rollback, contracts::DataPartition::development,
               suite.suite_id, "active configuration completed fewer development cases than baseline",
               baseline.completed, active.completed);
        audit_log_.back().candidate_scores = candidate_scores;
        auto result = current_result(Decision::alarm_frozen,
                                     "objective regression detected; baseline restored and exploration frozen");
        result.baseline_completed = baseline.completed;
        result.selected_completed = active.completed;
        result.evaluated_node_limits.assign(kNodeLimits.begin(), kNodeLimits.end());
        result.candidate_scores = candidate_scores;
        return result;
    }

    // Primary objective: maximize exact development cases completed. The
    // deterministic tie-break selects the smallest node cap that attains that
    // completion count. Grid order is ascending, so equal scores keep the first.
    auto selected = evaluations.begin();
    for (auto candidate = std::next(evaluations.begin()); candidate != evaluations.end(); ++candidate) {
        if (candidate->second.completed > selected->second.completed) selected = candidate;
    }

    SearchResult result;
    result.state = state_;
    result.baseline_completed = baseline.completed;
    result.selected_completed = selected->second.completed;
    result.evaluated_node_limits.assign(kNodeLimits.begin(), kNodeLimits.end());
    if (selected->first != active_node_limit_) {
        const auto previous = active_version_;
        active_node_limit_ = selected->first;
        if (active_node_limit_ == kBaselineNodeLimit) {
            active_version_ = options_.baseline_version;
        } else {
            ++version_counter_;
            active_version_ = "wmc-node-limit-v1-" + std::to_string(active_node_limit_) +
                              "-run-" + std::to_string(version_counter_);
        }
        add_event({0, EventKind::search_updated, contracts::DataPartition::development,
                   suite.suite_id, previous, active_version_, baseline.completed,
                   selected->second.completed, std::nullopt, std::nullopt,
                   "selected finite node cap by development completion count; ties prefer lower cap",
                   candidate_scores});
        result.decision = Decision::updated;
        result.diagnostic = "development-only search selected a bounded node cap";
    } else {
        add_event({0, EventKind::search_unchanged, contracts::DataPartition::development,
                   suite.suite_id, active_version_, active_version_, baseline.completed,
                   selected->second.completed, std::nullopt, std::nullopt,
                   "current configuration remains the deterministic development optimum",
                   candidate_scores});
        result.decision = Decision::unchanged;
        result.diagnostic = "current bounded configuration remains selected";
    }
    result.state = state_;
    result.active_node_limit = active_node_limit_;
    result.active_version = active_version_;
    result.candidate_scores = candidate_scores;
    return result;
}

MonitorResult Controller::observe(const StreamingObservation& observation) {
    // Reject held-out records before inspecting observation contents.
    if (observation.partition != contracts::DataPartition::streaming) {
        add_event({0, EventKind::partition_rejected, observation.partition, {}, active_version_,
                   active_version_, std::nullopt, std::nullopt, std::nullopt,
                   std::nullopt, "monitor accepts streaming records only", {}});
        return current_monitor_result(Decision::rejected_partition, false, std::nullopt,
                                      "only streaming residuals may reach the change detector");
    }
    if (!observation.observation_id.valid() || !observation.partition_id.valid()) {
        return current_monitor_result(Decision::invalid_input, false, std::nullopt,
                                      "invalid streaming observation or partition ID");
    }
    if (state_ == ControllerState::frozen) {
        if (hold_off_remaining_ > 0) --hold_off_remaining_;
        return current_monitor_result(Decision::alarm_frozen, false, std::nullopt,
                                      "baseline remains selected during explicit hold-off");
    }
    if (!residual_reference_) {
        return current_monitor_result(Decision::invalid_input, false, std::nullopt,
                                      "run development search before monitoring streaming residuals");
    }
    if (!observation.solver_success || observation.recursive_nodes == 0 ||
        observation.recursive_nodes > kMaximumNodeLimit) {
        freeze(EventKind::invariant_rollback, contracts::DataPartition::streaming,
               observation.observation_id.value(),
               "streaming solve failed or reported a node count outside the declared cap");
        return current_monitor_result(Decision::alarm_frozen, false, std::nullopt,
                                      "streaming invariant failed; baseline restored and exploration frozen");
    }

    ++accepted_stream_samples_;
    const bool sampled = accepted_stream_samples_ % options_.detector.sample_cadence == 0;
    if (!sampled) {
        return current_monitor_result(Decision::unchanged, false, std::nullopt,
                                      "sample cadence has not been reached");
    }
    const double residual = std::log1p(static_cast<double>(observation.recursive_nodes)) -
                            *residual_reference_;
    if (!std::isfinite(residual)) {
        freeze(EventKind::invariant_rollback, contracts::DataPartition::streaming,
               observation.observation_id.value(), "non-finite monitored residual");
        return current_monitor_result(Decision::alarm_frozen, true, std::nullopt,
                                      "invalid residual; baseline restored and exploration frozen");
    }
    const double next_cusum = std::max(0.0, cusum_ +
        gaussian_log_likelihood_ratio(residual, options_.detector));
    if (!std::isfinite(next_cusum)) {
        freeze(EventKind::invariant_rollback, contracts::DataPartition::streaming,
               observation.observation_id.value(), "non-finite CUSUM state", std::nullopt,
               std::nullopt, residual);
        return current_monitor_result(Decision::alarm_frozen, true, residual,
                                      "invalid detector state; baseline restored and exploration frozen");
    }
    cusum_ = next_cusum;
    if (cusum_ > options_.detector.threshold) {
        freeze(EventKind::shift_alarm_rollback, contracts::DataPartition::streaming,
               observation.observation_id.value(), "one-sided Gaussian CUSUM crossed threshold",
               std::nullopt, std::nullopt, residual, cusum_);
        return current_monitor_result(Decision::alarm_frozen, true, residual,
                                      "shift alarm; baseline restored and exploration frozen");
    }
    add_event({0, EventKind::residual_observed, contracts::DataPartition::streaming,
               observation.observation_id.value(), active_version_, active_version_,
               std::nullopt, std::nullopt, residual, cusum_,
               "streaming residual monitored at configured cadence", {}});
    return current_monitor_result(Decision::unchanged, true, residual,
                                  "residual is below the configured CUSUM alarm threshold");
}

Decision Controller::reset() {
    if (state_ != ControllerState::frozen || hold_off_remaining_ != 0) {
        return Decision::reset_blocked;
    }
    const auto previous = active_version_;
    state_ = ControllerState::exploring;
    active_node_limit_ = kBaselineNodeLimit;
    active_version_ = options_.baseline_version;
    cusum_ = 0.0;
    accepted_stream_samples_ = 0;
    add_event({0, EventKind::reset, contracts::DataPartition::streaming, {}, previous,
               active_version_, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
               "explicit operator reset after all hold-off samples; exploration restarts at baseline",
               {}});
    return Decision::reset;
}

}  // namespace xai::adaptation
