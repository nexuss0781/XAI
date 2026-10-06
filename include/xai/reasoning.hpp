#pragma once

#include "xai/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <gmpxx.h>

namespace xai::reasoning {

using Rational = mpq_class;
using Assignment = std::map<std::string, std::string, std::less<>>;

struct Variable {
    std::string id;
    std::vector<std::string> domain;
    // Empty means unit weight for every value; otherwise aligned with domain.
    std::vector<Rational> weights;
};

struct TableConstraint {
    std::vector<std::string> scope;
    // An assignment to scope is allowed iff its value tuple occurs here.
    std::vector<std::vector<std::string>> allowed_tuples;
};

struct UnsupportedConstraint {
    std::string feature;
};

using HardConstraint = std::variant<TableConstraint, UnsupportedConstraint>;

struct FiniteTask {
    std::vector<Variable> variables;
    std::vector<HardConstraint> constraints;
    Assignment evidence;
    Assignment query;
};

struct SolverLimits {
    std::size_t max_variables{64};
    std::size_t max_constraints{10'000};
    std::size_t max_domain_values{1'000'000};
    std::size_t max_symbol_bytes{256};
    std::size_t max_table_rows{1'000'000};
    std::size_t max_table_cells{1'000'000};
    std::uint64_t max_assignments{1'000'000};
    std::uint64_t max_operations{10'000'000};
    // A zero-millisecond budget is an already-expired deadline.
    std::uint64_t timeout_ms{10'000};
};

struct SolverStats {
    std::uint64_t assignments_examined{0};
    std::uint64_t constraint_checks{0};
    std::uint64_t operations{0};
    std::uint64_t elapsed_ms{0};
};

struct SolveResult {
    contracts::Status status{contracts::Status::unknown};
    std::optional<bool> satisfiable;
    std::optional<Rational> numerator;
    std::optional<Rational> denominator;
    std::optional<Rational> probability;
    SolverStats stats;
    std::string diagnostic;
};

[[nodiscard]] SolveResult solve_finite(const FiniteTask& task,
                                       const SolverLimits& limits = {});

struct CausalDeclaration {
    bool scm_declared{false};
    bool graph_declared{false};
    bool mechanisms_declared{false};
    bool identification_conditions_declared{false};
    bool identification_conditions_satisfied{false};
};

struct CausalPolicyResult {
    contracts::Status status{contracts::Status::unknown};
    std::string diagnostic;
};

// This phase has no causal estimator. The policy never emits an intervention value.
[[nodiscard]] CausalPolicyResult assess_causal_request(
    const CausalDeclaration& declaration);

}  // namespace xai::reasoning
