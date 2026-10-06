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

namespace xai::decision {

using Rational = mpq_class;

inline constexpr std::string_view kWmcExactResultProperty =
    "xai.wmc.exact-rational-result.v1";
inline constexpr std::string_view kWmcExactOutcomeState = "exact";
inline constexpr std::uint64_t kWmcCpuBudgetMs = 600'000;
inline constexpr std::uint64_t kWmcMemoryBudgetBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kMaximumDecisionStates = 256;
inline constexpr std::size_t kMaximumCandidateActions = 256;

enum class DecisionStatus { selected, abstention, invalid_input };
enum class CertificateStatus {
    not_required,
    not_evaluated,
    verified,
    missing,
    invalid,
    stale,
    mismatch,
    verifier_rejected,
    resource_limit
};

[[nodiscard]] std::string_view to_string(DecisionStatus value) noexcept;
[[nodiscard]] std::string_view to_string(CertificateStatus value) noexcept;

struct StateProbability {
    std::string state_id;
    Rational probability{0};
    bool unresolved{false};
};

// The certificate binds an exact claim to the input and emitted result. A verifier
// must independently establish the named property; metadata matching alone is not
// verification.
struct Certificate {
    contracts::CertificateId certificate_id;
    std::string action_id;
    std::string subject_sha256;
    std::string claim_sha256;
    std::string property_id;
    std::string verifier_id;
    std::string verifier_version;
    std::uint64_t valid_from_epoch{0};
    std::uint64_t expires_at_epoch{0};
};

struct VerificationResult {
    bool accepted{false};
    std::string verified_property_id;
    std::string diagnostic;
};

class CertificateVerifier {
public:
    virtual ~CertificateVerifier() = default;
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view version() const noexcept = 0;
    [[nodiscard]] virtual VerificationResult verify(
        const Certificate& certificate, std::string_view expected_property_id,
        std::string_view expected_subject_sha256,
        std::string_view expected_claim_sha256,
        const contracts::CanonicalValue& output_value) const = 0;
};

struct CandidateAction {
    std::string action_id;
    std::string label;
    // Structured value returned if selected; exact numbers must use a task-defined
    // exact encoding rather than floating-point JSON.
    std::optional<contracts::CanonicalValue> output_value;
    // Must contain exactly one non-negative loss for every declared state.
    std::map<std::string, Rational, std::less<>> loss_by_state;
    // Normalized resource cost. For this task, cost = max(CPU/budget, peak-RAM/budget).
    Rational resource_cost{0};
    std::int32_t tie_break_priority{0};
    // Lowercase SHA-256 of the exact canonical output being certified.
    std::string claim_sha256;
    bool requires_certificate{false};
    std::string required_property_id;
    std::optional<Certificate> certificate;
};

// Constructs the task-specific `wmc.emit_exact_result` action. Its loss is zero
// only for the resolved `exact` state and one for every other declared state.
// A certificate remains mandatory even when this helper is used.
[[nodiscard]] CandidateAction make_wmc_exact_result_action(
    const std::vector<StateProbability>& states, Rational resource_cost,
    contracts::CanonicalValue output_value, std::string claim_sha256,
    std::optional<Certificate> certificate = std::nullopt);

struct DecisionPolicy {
    // Risk = expected state loss + resource_cost_weight * resource_cost.
    Rational resource_cost_weight{1};
    Rational abstention_cost{1};
    // If unresolved probability is above this explicit allowance, force abstention.
    Rational maximum_unresolved_probability{0};
    bool require_exact_output{true};
    std::string verifier_id;
    std::string verifier_version;
    std::uint64_t decision_time_epoch{0};
};

struct DecisionRequest {
    std::string subject_sha256;
    std::vector<StateProbability> states;
    std::vector<CandidateAction> actions;
    contracts::Status upstream_status{contracts::Status::success};
    contracts::Exactness output_exactness{contracts::Exactness::exact};
    std::vector<std::string> lineage;
    std::vector<std::string> assumptions;
    std::vector<std::string> limitations;
};

struct ActionAssessment {
    std::string action_id;
    CertificateStatus certificate_status{CertificateStatus::not_evaluated};
    bool admissible{false};
    std::optional<Rational> expected_risk;
    std::optional<Rational> resource_cost;
    std::string diagnostic;
};

struct DecisionOutput {
    DecisionStatus status{DecisionStatus::invalid_input};
    contracts::Status upstream_status{contracts::Status::unknown};
    contracts::Exactness exactness{contracts::Exactness::not_applicable};
    std::optional<std::string> action_id;
    std::optional<std::string> action_label;
    std::optional<contracts::CanonicalValue> action_value;
    std::optional<std::string> claim_sha256;
    std::optional<Rational> expected_risk;
    std::optional<Rational> resource_cost;
    std::optional<Rational> unresolved_probability;
    std::string diagnostic;
    std::vector<ActionAssessment> assessments;
    std::vector<std::string> lineage;
    std::vector<std::string> assumptions;
    std::vector<std::string> limitations;
};

// Performs exact rational validation and risk comparison. Invalid input produces
// an explicit invalid_input result; unsupported/failed upstream results and
// unresolved mass produce an explicit abstention. Certificate-required actions
// are inadmissible unless every binding and verifier check passes.
[[nodiscard]] DecisionOutput decide(
    const DecisionRequest& request, const DecisionPolicy& policy,
    const CertificateVerifier* verifier = nullptr);

// Canonical JSON output; exact rationals use the shared reduced $rational object
// representation. The output describes policy execution and action/abstention.
[[nodiscard]] std::string serialize(const DecisionOutput& output);

// Phase 0 resource normalization: max(CPU ms / 600,000, peak bytes / 4 GiB).
// Values above 1 are valid measurements but exceed the declared hard budget.
[[nodiscard]] Rational normalized_wmc_resource_cost(
    std::uint64_t cpu_time_ms, std::uint64_t peak_memory_bytes);

}  // namespace xai::decision
