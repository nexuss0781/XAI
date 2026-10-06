#include "xai/decision.hpp"

#include <algorithm>
#include <exception>
#include <set>
#include <utility>

namespace xai::decision {
namespace {

[[nodiscard]] bool valid_rational(const Rational& value) {
    return value.get_den() > 0;
}

[[nodiscard]] bool valid_digest(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

[[nodiscard]] contracts::CanonicalValue rational_value(
    const std::optional<Rational>& value) {
    if (!value) return nullptr;
    Rational canonical = *value;
    canonical.canonicalize();
    contracts::CanonicalValue::Array fraction{
        contracts::CanonicalValue{canonical.get_num().get_str()},
        contracts::CanonicalValue{canonical.get_den().get_str()}};
    return contracts::CanonicalValue::Object{
        {"$rational", contracts::CanonicalValue{std::move(fraction)}}};
}

[[nodiscard]] contracts::CanonicalValue string_array(
    const std::vector<std::string>& values) {
    contracts::CanonicalValue::Array result;
    result.reserve(values.size());
    for (const auto& value : values) result.emplace_back(value);
    return result;
}

[[nodiscard]] DecisionOutput invalid_output(const DecisionRequest& request,
                                           std::string diagnostic) {
    DecisionOutput output;
    output.status = DecisionStatus::invalid_input;
    output.upstream_status = contracts::to_string(request.upstream_status) == "invalid_status"
        ? contracts::Status::invalid_schema : request.upstream_status;
    output.exactness = contracts::Exactness::not_applicable;
    output.diagnostic = std::move(diagnostic);
    output.lineage = request.lineage;
    output.assumptions = request.assumptions;
    output.limitations = request.limitations;
    output.limitations.emplace_back("No action was emitted because decision input or policy validation failed.");
    return output;
}

[[nodiscard]] DecisionOutput abstain_output(const DecisionRequest& request,
                                            const DecisionPolicy& policy,
                                            std::string diagnostic,
                                            std::optional<Rational> unresolved = std::nullopt,
                                            bool include_action_assessments = false) {
    DecisionOutput output;
    output.status = DecisionStatus::abstention;
    output.upstream_status = request.upstream_status;
    output.exactness = contracts::Exactness::not_applicable;
    output.expected_risk = policy.abstention_cost;
    output.resource_cost = Rational{0};
    output.unresolved_probability = std::move(unresolved);
    output.diagnostic = std::move(diagnostic);
    output.lineage = request.lineage;
    output.assumptions = request.assumptions;
    output.limitations = request.limitations;
    output.limitations.emplace_back("No candidate action was emitted; abstention is an explicit decision outcome.");
    if (include_action_assessments) {
        for (const auto& action : request.actions) {
            output.assessments.push_back(ActionAssessment{
                action.action_id,
                action.requires_certificate ? CertificateStatus::not_evaluated
                                            : CertificateStatus::not_required,
                false, std::nullopt, action.resource_cost,
                "Action evaluation was skipped because the request requires abstention."});
        }
        std::sort(output.assessments.begin(), output.assessments.end(),
                  [](const auto& left, const auto& right) {
                      return left.action_id < right.action_id;
                  });
    }
    return output;
}

[[nodiscard]] ActionAssessment check_certificate(
    const CandidateAction& action, const DecisionRequest& request,
    const DecisionPolicy& policy, const CertificateVerifier* verifier) {
    ActionAssessment assessment;
    assessment.action_id = action.action_id;
    assessment.resource_cost = action.resource_cost;
    if (!action.requires_certificate) {
        assessment.certificate_status = CertificateStatus::not_required;
        assessment.admissible = true;
        return assessment;
    }
    if (!action.certificate) {
        assessment.certificate_status = CertificateStatus::missing;
        assessment.diagnostic = "Required certificate is absent.";
        return assessment;
    }

    const auto& certificate = *action.certificate;
    if (!certificate.certificate_id.valid() || !valid_digest(certificate.subject_sha256) ||
        !valid_digest(certificate.claim_sha256) ||
        !contracts::valid_identifier(certificate.action_id) ||
        !contracts::valid_identifier(certificate.property_id) ||
        !contracts::valid_identifier(certificate.verifier_id) ||
        !contracts::valid_identifier(certificate.verifier_version) ||
        certificate.valid_from_epoch > certificate.expires_at_epoch) {
        assessment.certificate_status = CertificateStatus::invalid;
        assessment.diagnostic = "Certificate metadata is malformed.";
        return assessment;
    }
    if (policy.decision_time_epoch < certificate.valid_from_epoch ||
        policy.decision_time_epoch > certificate.expires_at_epoch) {
        assessment.certificate_status = CertificateStatus::stale;
        assessment.diagnostic = "Certificate is not valid at the decision time.";
        return assessment;
    }
    if (certificate.action_id != action.action_id ||
        certificate.subject_sha256 != request.subject_sha256 ||
        certificate.claim_sha256 != action.claim_sha256 ||
        certificate.property_id != action.required_property_id ||
        certificate.verifier_id != policy.verifier_id ||
        certificate.verifier_version != policy.verifier_version) {
        assessment.certificate_status = CertificateStatus::mismatch;
        assessment.diagnostic = "Certificate binding does not match action, input, claim, property, or verifier policy.";
        return assessment;
    }
    if (verifier == nullptr || verifier->id() != policy.verifier_id ||
        verifier->version() != policy.verifier_version) {
        assessment.certificate_status = CertificateStatus::verifier_rejected;
        assessment.diagnostic = "Configured certificate verifier is unavailable or has the wrong identity/version.";
        return assessment;
    }

    try {
        const auto result = verifier->verify(certificate, action.required_property_id,
                                             request.subject_sha256, action.claim_sha256,
                                             *action.output_value);
        if (!result.accepted || result.verified_property_id != action.required_property_id) {
            assessment.certificate_status = CertificateStatus::verifier_rejected;
            assessment.diagnostic = result.diagnostic.empty()
                ? "Verifier did not establish the required property."
                : result.diagnostic;
            return assessment;
        }
    } catch (const std::exception& error) {
        assessment.certificate_status = CertificateStatus::verifier_rejected;
        assessment.diagnostic = std::string("Verifier failed: ") + error.what();
        return assessment;
    } catch (...) {
        assessment.certificate_status = CertificateStatus::verifier_rejected;
        assessment.diagnostic = "Verifier failed with a non-standard exception.";
        return assessment;
    }

    assessment.certificate_status = CertificateStatus::verified;
    assessment.admissible = true;
    assessment.diagnostic = "Required property was accepted by the configured verifier.";
    return assessment;
}

[[nodiscard]] contracts::CanonicalValue assessment_value(
    const ActionAssessment& assessment) {
    contracts::CanonicalValue::Object object{
        {"action_id", assessment.action_id},
        {"admissible", assessment.admissible},
        {"certificate_status", std::string(to_string(assessment.certificate_status))},
        {"diagnostic", assessment.diagnostic},
        {"expected_risk", rational_value(assessment.expected_risk)},
        {"resource_cost", rational_value(assessment.resource_cost)}};
    return object;
}

}  // namespace

std::string_view to_string(DecisionStatus value) noexcept {
    switch (value) {
    case DecisionStatus::selected: return "selected";
    case DecisionStatus::abstention: return "abstention";
    case DecisionStatus::invalid_input: return "invalid_input";
    }
    return "invalid_decision_status";
}

std::string_view to_string(CertificateStatus value) noexcept {
    switch (value) {
    case CertificateStatus::not_required: return "not_required";
    case CertificateStatus::not_evaluated: return "not_evaluated";
    case CertificateStatus::verified: return "verified";
    case CertificateStatus::missing: return "missing";
    case CertificateStatus::invalid: return "invalid";
    case CertificateStatus::stale: return "stale";
    case CertificateStatus::mismatch: return "mismatch";
    case CertificateStatus::verifier_rejected: return "verifier_rejected";
    case CertificateStatus::resource_limit: return "resource_limit";
    }
    return "invalid_certificate_status";
}

CandidateAction make_wmc_exact_result_action(
    const std::vector<StateProbability>& states, Rational resource_cost,
    contracts::CanonicalValue output_value, std::string claim_sha256,
    std::optional<Certificate> certificate) {
    CandidateAction action;
    action.action_id = "wmc.emit_exact_result";
    action.label = "Return exact rational WMC result";
    action.output_value = std::move(output_value);
    action.resource_cost = std::move(resource_cost);
    action.claim_sha256 = std::move(claim_sha256);
    action.requires_certificate = true;
    action.required_property_id = std::string(kWmcExactResultProperty);
    action.certificate = std::move(certificate);
    for (const auto& state : states) {
        const bool resolved_exact = state.state_id == kWmcExactOutcomeState && !state.unresolved;
        action.loss_by_state.emplace(state.state_id, resolved_exact ? Rational{0} : Rational{1});
    }
    return action;
}

DecisionOutput decide(const DecisionRequest& request, const DecisionPolicy& policy,
                      const CertificateVerifier* verifier) {
    if (contracts::to_string(request.upstream_status) == "invalid_status" ||
        contracts::to_string(request.output_exactness) == "invalid_exactness") {
        return invalid_output(request, "Request contains an invalid upstream status or exactness enum.");
    }
    if (!valid_rational(policy.resource_cost_weight) || policy.resource_cost_weight < 0 ||
        !valid_rational(policy.abstention_cost) || policy.abstention_cost < 0 ||
        !valid_rational(policy.maximum_unresolved_probability) ||
        policy.maximum_unresolved_probability < 0 ||
        policy.maximum_unresolved_probability > 1) {
        return invalid_output(request, "Decision policy contains an invalid cost or unresolved-mass bound.");
    }

    if (request.upstream_status != contracts::Status::success) {
        return abstain_output(request, policy,
            "Upstream status is " + std::string(contracts::to_string(request.upstream_status)) +
            "; no candidate answer is eligible.");
    }
    if (policy.require_exact_output && request.output_exactness != contracts::Exactness::exact) {
        return abstain_output(request, policy,
            "The task policy requires an exact result; approximate or unspecified output is not eligible.");
    }
    if (request.output_exactness == contracts::Exactness::not_applicable) {
        return invalid_output(request, "A candidate output must declare exact or approximate exactness.");
    }
    if (request.states.size() > kMaximumDecisionStates ||
        request.actions.size() > kMaximumCandidateActions) {
        return invalid_output(request, "Decision request exceeds the 256-state or 256-action cap.");
    }

    std::set<std::string, std::less<>> state_ids;
    Rational total_probability{0};
    Rational unresolved_probability{0};
    for (const auto& state : request.states) {
        if (!contracts::valid_identifier(state.state_id) || !state_ids.insert(state.state_id).second ||
            !valid_rational(state.probability) || state.probability < 0) {
            return invalid_output(request, "State IDs must be unique and probabilities must be valid non-negative rationals.");
        }
        total_probability += state.probability;
        if (state.unresolved) unresolved_probability += state.probability;
    }
    if (request.states.empty() || total_probability != 1) {
        return invalid_output(request, "State probabilities must form a non-empty distribution summing exactly to one.");
    }
    std::set<std::string, std::less<>> action_ids;
    std::set<std::string, std::less<>> certificate_ids;
    bool has_certificate_action = false;
    for (const auto& action : request.actions) {
        if (!contracts::valid_identifier(action.action_id) ||
            !action_ids.insert(action.action_id).second ||
            !valid_rational(action.resource_cost) || action.resource_cost < 0 ||
            action.label.empty() || !action.output_value ||
            std::holds_alternative<std::nullptr_t>(action.output_value->data) ||
            !valid_digest(action.claim_sha256)) {
            return invalid_output(request, "Actions need unique valid IDs, a label, a structured value, an output digest, and non-negative rational resource cost.");
        }
        try {
            static_cast<void>(contracts::encode_canonical_value(*action.output_value));
            static_cast<void>(contracts::encode_canonical_value(
                contracts::CanonicalValue{action.label}));
        } catch (const std::exception& error) {
            return invalid_output(request, std::string("Action output is not canonicalizable: ") + error.what());
        }
        if (action.certificate &&
            (!action.certificate->certificate_id.valid() ||
             !certificate_ids.insert(action.certificate->certificate_id.value()).second)) {
            return invalid_output(request, "Attached certificate IDs must be valid and unique within a decision request.");
        }
        if (action.loss_by_state.size() != state_ids.size()) {
            return invalid_output(request, "Every action must specify exactly one loss for every declared state.");
        }
        for (const auto& state_id : state_ids) {
            const auto loss = action.loss_by_state.find(state_id);
            if (loss == action.loss_by_state.end() || !valid_rational(loss->second) || loss->second < 0) {
                return invalid_output(request, "Action losses must cover all states and be valid non-negative rationals.");
            }
        }
        if (action.requires_certificate) {
            has_certificate_action = true;
            if (!contracts::valid_identifier(action.required_property_id) ||
                !valid_digest(request.subject_sha256) || !valid_digest(action.claim_sha256)) {
                return invalid_output(request, "Certificate-required actions need a property ID and lowercase SHA-256 input/claim digests.");
            }
        }
    }
    if (has_certificate_action &&
        (!contracts::valid_identifier(policy.verifier_id) ||
         !contracts::valid_identifier(policy.verifier_version))) {
        return invalid_output(request, "Certificate policy must declare a valid verifier identity and version.");
    }
    if (unresolved_probability > policy.maximum_unresolved_probability) {
        return abstain_output(request, policy,
            "Unresolved probability exceeds the policy allowance; abstention is forced.",
            unresolved_probability, true);
    }

    std::vector<const CandidateAction*> sorted_actions;
    sorted_actions.reserve(request.actions.size());
    for (const auto& action : request.actions) sorted_actions.push_back(&action);
    std::sort(sorted_actions.begin(), sorted_actions.end(), [](const auto* left, const auto* right) {
        return left->action_id < right->action_id;
    });

    DecisionOutput output;
    output.upstream_status = request.upstream_status;
    output.exactness = contracts::Exactness::not_applicable;
    output.unresolved_probability = unresolved_probability;
    output.lineage = request.lineage;
    output.assumptions = request.assumptions;
    output.limitations = request.limitations;

    const CandidateAction* best_action = nullptr;
    Rational best_risk{0};
    for (const auto* action : sorted_actions) {
        ActionAssessment assessment;
        assessment.action_id = action->action_id;
        assessment.resource_cost = action->resource_cost;
        if (action->resource_cost > 1) {
            assessment.certificate_status = CertificateStatus::resource_limit;
            assessment.diagnostic = "Normalized resource cost exceeds the Phase 0 hard budget.";
        } else {
            assessment = check_certificate(*action, request, policy, verifier);
        }
        if (assessment.admissible) {
            Rational expected_loss{0};
            for (const auto& state : request.states)
                expected_loss += state.probability * action->loss_by_state.at(state.state_id);
            const Rational risk = expected_loss + policy.resource_cost_weight * action->resource_cost;
            assessment.expected_risk = risk;
            if (best_action == nullptr || risk < best_risk ||
                (risk == best_risk &&
                 (action->tie_break_priority < best_action->tie_break_priority ||
                  (action->tie_break_priority == best_action->tie_break_priority &&
                   action->action_id < best_action->action_id)))) {
                best_action = action;
                best_risk = risk;
            }
        }
        output.assessments.push_back(std::move(assessment));
    }

    if (best_action == nullptr) {
        output.status = DecisionStatus::abstention;
        output.expected_risk = policy.abstention_cost;
        output.resource_cost = Rational{0};
        output.diagnostic = "No admissible candidate remains after certificate and resource-policy checks.";
        output.limitations.emplace_back("The policy did not produce an exact WMC count; no rejected action is described as certified.");
        return output;
    }
    if (best_risk >= policy.abstention_cost) {
        output.status = DecisionStatus::abstention;
        output.expected_risk = policy.abstention_cost;
        output.resource_cost = Rational{0};
        output.diagnostic = "Abstention has lower risk, or ties the best action and wins the declared conservative tie-break.";
        output.limitations.emplace_back("The policy did not emit a candidate action because abstention was preferred.");
        return output;
    }

    output.status = DecisionStatus::selected;
    output.action_id = best_action->action_id;
    output.action_label = best_action->label;
    output.action_value = best_action->output_value;
    output.claim_sha256 = best_action->claim_sha256;
    output.expected_risk = best_risk;
    output.resource_cost = best_action->resource_cost;
    output.exactness = request.output_exactness;
    output.diagnostic = "Selected the lowest-risk admissible action; certificate requirements were checked before ranking.";
    if (best_action->requires_certificate)
        output.limitations.emplace_back("The certificate establishes only its encoded property under the configured verifier's assumptions.");
    else
        output.limitations.emplace_back("The selected action did not require a certificate; no certified claim is made.");
    return output;
}

std::string serialize(const DecisionOutput& output) {
    if (to_string(output.status) == "invalid_decision_status" ||
        contracts::to_string(output.upstream_status) == "invalid_status" ||
        contracts::to_string(output.exactness) == "invalid_exactness")
        throw std::invalid_argument("decision output contains an invalid status enum");
    const auto valid_nonnegative_optional = [](const std::optional<Rational>& value) {
        return !value || (valid_rational(*value) && *value >= 0);
    };
    if (!valid_nonnegative_optional(output.expected_risk) ||
        !valid_nonnegative_optional(output.resource_cost) ||
        !valid_nonnegative_optional(output.unresolved_probability) ||
        (output.unresolved_probability && *output.unresolved_probability > 1))
        throw std::invalid_argument("decision output contains an invalid exact numeric value");
    if (output.action_id && !contracts::valid_identifier(*output.action_id))
        throw std::invalid_argument("decision output contains an invalid action ID");
    if (output.claim_sha256 && !valid_digest(*output.claim_sha256))
        throw std::invalid_argument("decision output contains an invalid action digest");
    if ((output.status == DecisionStatus::selected &&
         (!output.action_id || !output.action_label || output.action_label->empty() ||
          !output.action_value ||
          std::holds_alternative<std::nullptr_t>(output.action_value->data) ||
          !output.claim_sha256 || !output.expected_risk || !output.resource_cost ||
          output.exactness == contracts::Exactness::not_applicable)) ||
        (output.status == DecisionStatus::abstention &&
         (output.action_id || output.action_label || output.action_value || output.claim_sha256 ||
          !output.expected_risk || !output.resource_cost ||
          output.exactness != contracts::Exactness::not_applicable)) ||
        (output.status == DecisionStatus::invalid_input &&
         (output.action_id || output.action_label || output.action_value || output.claim_sha256 ||
          output.expected_risk || output.resource_cost ||
          output.exactness != contracts::Exactness::not_applicable)))
        throw std::invalid_argument("decision output fields do not match its declared outcome");
    for (const auto& assessment : output.assessments) {
        if (!contracts::valid_identifier(assessment.action_id) ||
            to_string(assessment.certificate_status) == "invalid_certificate_status" ||
            !valid_nonnegative_optional(assessment.expected_risk) ||
            !valid_nonnegative_optional(assessment.resource_cost))
            throw std::invalid_argument("decision output contains an invalid action assessment");
    }

    contracts::CanonicalValue::Array assessments;
    assessments.reserve(output.assessments.size());
    for (const auto& assessment : output.assessments)
        assessments.push_back(assessment_value(assessment));

    contracts::CanonicalValue::Object object{
        {"action_assessments", std::move(assessments)},
        {"action_id", output.action_id ? contracts::CanonicalValue{*output.action_id}
                                       : contracts::CanonicalValue{nullptr}},
        {"action_label", output.action_label ? contracts::CanonicalValue{*output.action_label}
                                             : contracts::CanonicalValue{nullptr}},
        {"action_value", output.action_value ? *output.action_value
                                             : contracts::CanonicalValue{nullptr}},
        {"assumptions", string_array(output.assumptions)},
        {"claim_sha256", output.claim_sha256 ? contracts::CanonicalValue{*output.claim_sha256}
                                             : contracts::CanonicalValue{nullptr}},
        {"decision_schema_id", "xai.decision.output"},
        {"decision_schema_version", 1U},
        {"diagnostic", output.diagnostic},
        {"exactness", std::string(contracts::to_string(output.exactness))},
        {"expected_risk", rational_value(output.expected_risk)},
        {"limitations", string_array(output.limitations)},
        {"lineage", string_array(output.lineage)},
        {"resource_cost", rational_value(output.resource_cost)},
        {"status", std::string(to_string(output.status))},
        {"unresolved_probability", rational_value(output.unresolved_probability)},
        {"upstream_status", std::string(contracts::to_string(output.upstream_status))}};
    return contracts::encode_canonical_value(contracts::CanonicalValue{std::move(object)});
}

Rational normalized_wmc_resource_cost(std::uint64_t cpu_time_ms,
                                      std::uint64_t peak_memory_bytes) {
    Rational cpu_cost{mpz_class{std::to_string(cpu_time_ms)},
                      mpz_class{std::to_string(kWmcCpuBudgetMs)}};
    Rational memory_cost{mpz_class{std::to_string(peak_memory_bytes)},
                         mpz_class{std::to_string(kWmcMemoryBudgetBytes)}};
    cpu_cost.canonicalize();
    memory_cost.canonicalize();
    return cpu_cost > memory_cost ? cpu_cost : memory_cost;
}

}  // namespace xai::decision
