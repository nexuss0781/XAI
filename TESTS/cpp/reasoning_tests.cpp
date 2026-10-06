#include "xai/reasoning.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace xai::reasoning;
using xai::contracts::Status;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] Rational fraction(unsigned long numerator, unsigned long denominator) {
    Rational value(numerator, denominator);
    value.canonicalize();
    return value;
}

struct OracleResult {
    bool satisfiable{false};
    Rational numerator{0};
    Rational denominator{0};
    std::optional<Rational> probability;
    Status status{Status::unknown};
};

[[nodiscard]] bool event_matches(const Assignment& event, const Assignment& assignment) {
    for (const auto& [variable, value] : event) {
        const auto found = assignment.find(variable);
        if (found == assignment.end() || found->second != value) return false;
    }
    return true;
}

// Deliberately simple recursive Cartesian-product oracle, independent of the solver's
// mixed-radix iterator and compiled table representation.
[[nodiscard]] OracleResult enumerate_independently(const FiniteTask& task) {
    Assignment assignment;
    OracleResult result;
    const auto visit = [&](const auto& self, std::size_t position) -> void {
        if (position != task.variables.size()) {
            const auto& variable = task.variables[position];
            for (const auto& value : variable.domain) {
                assignment[variable.id] = value;
                self(self, position + 1);
            }
            assignment.erase(task.variables[position].id);
            return;
        }

        if (!event_matches(task.evidence, assignment)) return;
        for (const auto& constraint : task.constraints) {
            const auto* table = std::get_if<TableConstraint>(&constraint);
            if (table == nullptr) throw std::runtime_error("oracle received unsupported constraint");
            std::vector<std::string> candidate;
            for (const auto& variable : table->scope) candidate.push_back(assignment.at(variable));
            if (std::find(table->allowed_tuples.begin(), table->allowed_tuples.end(), candidate) ==
                table->allowed_tuples.end()) return;
        }

        result.satisfiable = true;
        Rational mass{1};
        for (const auto& variable : task.variables) {
            if (variable.weights.empty()) continue;
            const auto value = std::find(variable.domain.begin(), variable.domain.end(),
                                         assignment.at(variable.id));
            mass *= variable.weights[static_cast<std::size_t>(value - variable.domain.begin())];
        }
        result.denominator += mass;
        if (event_matches(task.query, assignment)) result.numerator += mass;
    };
    visit(visit, 0);

    if (!result.satisfiable) {
        result.status = Status::inconsistent;
    } else if (result.denominator == 0) {
        result.status = Status::zero_normalizer;
    } else {
        result.status = Status::success;
        result.probability = result.numerator / result.denominator;
        result.probability->canonicalize();
    }
    return result;
}

void compare_with_oracle(const FiniteTask& task, const std::string& label) {
    const auto expected = enumerate_independently(task);
    const auto actual = solve_finite(task);
    require(actual.status == expected.status, label + ": status differs from independent oracle");
    require(actual.satisfiable == std::optional<bool>(expected.satisfiable),
            label + ": satisfiable result differs from independent oracle");
    require(actual.numerator == std::optional<Rational>(expected.numerator),
            label + ": numerator differs from independent oracle");
    require(actual.denominator == std::optional<Rational>(expected.denominator),
            label + ": denominator differs from independent oracle");
    require(actual.probability == expected.probability,
            label + ": conditional probability differs from independent oracle");
}

[[nodiscard]] FiniteTask binary_task() {
    FiniteTask task;
    task.variables = {
        {"x", {"0", "1"}, {fraction(1, 3), fraction(2, 3)}},
        {"y", {"0", "1"}, {fraction(2, 5), fraction(3, 5)}},
        {"z", {"0", "1"}, {Rational{1}, Rational{1}}},
    };
    return task;
}

void test_exact_conditional_probability() {
    FiniteTask task;
    task.variables = {
        {"x", {"A", "B"}, {fraction(1, 3), fraction(2, 3)}},
        {"y", {"no", "yes"}, {fraction(1, 2), fraction(1, 2)}},
    };
    task.constraints = {TableConstraint{
        {"x", "y"}, {{"A", "no"}, {"A", "yes"}, {"B", "yes"}}}};
    task.evidence = {{"x", "A"}};
    task.query = {{"y", "yes"}};

    const auto result = solve_finite(task);
    require(result.status == Status::success, "valid finite task must succeed");
    require(result.satisfiable == std::optional<bool>(true), "task must be structurally satisfiable");
    require(result.numerator == std::optional<Rational>(fraction(1, 6)),
            "query WMC must be exact 1/6");
    require(result.denominator == std::optional<Rational>(fraction(1, 3)),
            "evidence WMC must be exact 1/3");
    require(result.probability == std::optional<Rational>(fraction(1, 2)),
            "conditional probability must be exact 1/2");
    task.query = {{"y", "no"}};
    const auto complement = solve_finite(task);
    require(complement.status == Status::success &&
            complement.probability == std::optional<Rational>(fraction(1, 2)) &&
            *result.probability + *complement.probability == Rational{1},
            "complementary query probabilities must sum exactly to one");
    task.query = {{"y", "yes"}};
    compare_with_oracle(task, "conditional_probability");
}

void test_sat_unsat_zero_weight_and_empty_task() {
    FiniteTask empty;
    const auto empty_result = solve_finite(empty);
    require(empty_result.status == Status::success &&
            empty_result.probability == std::optional<Rational>(Rational{1}),
            "empty finite task has one empty assignment and probability one");

    FiniteTask unsatisfiable;
    unsatisfiable.variables = {{"x", {"0", "1"}, {}}};
    unsatisfiable.constraints = {TableConstraint{{"x"}, {}}};
    const auto unsat_result = solve_finite(unsatisfiable);
    require(unsat_result.status == Status::inconsistent &&
            unsat_result.satisfiable == std::optional<bool>(false),
            "empty allowed relation must be explicitly inconsistent");
    compare_with_oracle(unsatisfiable, "unsatisfiable_relation");

    FiniteTask zero_mass;
    zero_mass.variables = {{"x", {"0", "1"}, {Rational{0}, Rational{0}}}};
    const auto zero_result = solve_finite(zero_mass);
    require(zero_result.status == Status::zero_normalizer &&
            zero_result.satisfiable == std::optional<bool>(true),
            "zero total weight must remain distinct from structural UNSAT");
    require(zero_result.numerator == std::optional<Rational>(Rational{0}) &&
            zero_result.denominator == std::optional<Rational>(Rational{0}) &&
            !zero_result.probability,
            "zero-normalizer result has exact zero masses and no probability");
    compare_with_oracle(zero_mass, "zero_normalizer");
}

void test_query_edges_and_evidence_conflict() {
    FiniteTask task;
    task.variables = {{"x", {"0", "1"}, {Rational{1}, Rational{1}}}};
    task.evidence = {{"x", "0"}};
    task.query = {{"x", "1"}};
    const auto result = solve_finite(task);
    require(result.status == Status::success &&
            result.probability == std::optional<Rational>(Rational{0}),
            "query inconsistent with evidence is a valid zero-probability event");
    compare_with_oracle(task, "query_disjoint_from_evidence");

    task.evidence = {{"x", "missing"}};
    const auto invalid = solve_finite(task);
    require(invalid.status == Status::invalid_schema && !invalid.probability,
            "out-of-domain evidence must be rejected as invalid schema");
}

void test_exhaustive_three_boolean_relation_grid() {
    // Every one of the 256 possible hard relations over three Boolean variables.
    for (unsigned relation = 0; relation < 256; ++relation) {
        auto task = binary_task();
        TableConstraint table{{"x", "y", "z"}, {}};
        for (unsigned assignment = 0; assignment < 8; ++assignment) {
            if (((relation >> assignment) & 1U) == 0) continue;
            table.allowed_tuples.push_back({
                ((assignment >> 2U) & 1U) != 0 ? "1" : "0",
                ((assignment >> 1U) & 1U) != 0 ? "1" : "0",
                (assignment & 1U) != 0 ? "1" : "0"});
        }
        task.constraints = {std::move(table)};
        if ((relation % 3U) == 0) task.evidence = {{"x", "1"}};
        if ((relation % 5U) == 0) task.query = {{"z", "1"}};
        compare_with_oracle(task, "relation_truth_table_" + std::to_string(relation));
    }
}

void test_malformed_and_unsupported_inputs() {
    FiniteTask task;
    task.variables = {{"bad id", {"0", "1"}, {}}};
    require(solve_finite(task).status == Status::invalid_schema,
            "variable identifiers must use the shared typed-ID syntax");

    task.variables = {{"x", {"0", "0"}, {}}};
    require(solve_finite(task).status == Status::invalid_schema,
            "duplicate domain value must be invalid");

    task.variables = {{"x", {"0", "1"}, {Rational{1}}}};
    require(solve_finite(task).status == Status::invalid_schema,
            "weight/domain length mismatch must be invalid");

    task.variables = {{"x", {"0", "1"}, {Rational{1}, Rational{-1}}}};
    require(solve_finite(task).status == Status::invalid_schema,
            "negative weight must be rejected");

    task.variables = {{"x", {"0", "1"}, {}}};
    task.constraints = {TableConstraint{{"x"}, {{"outside"}}}};
    require(solve_finite(task).status == Status::invalid_schema,
            "out-of-domain table value must be invalid");

    task.constraints = {TableConstraint{{"x"}, {{"0", "1"}}}};
    require(solve_finite(task).status == Status::invalid_schema,
            "wrong table tuple arity must be invalid");

    task.constraints = {TableConstraint{{"unknown"}, {{"0"}}}};
    require(solve_finite(task).status == Status::invalid_schema,
            "undeclared constraint variable must be invalid");

    task.constraints = {UnsupportedConstraint{"non-finite arithmetic grounding"}};
    const auto unsupported = solve_finite(task);
    require(unsupported.status == Status::unsupported_input && !unsupported.probability,
            "unsupported grounding must have a distinct explicit status");
}

void test_budgets_and_no_partial_result() {
    FiniteTask task;
    task.variables = {
        {"x", {"0", "1"}, {}}, {"y", {"0", "1"}, {}}, {"z", {"0", "1"}, {}}};
    SolverLimits states;
    states.max_assignments = 4;
    const auto limited = solve_finite(task, states);
    require(limited.status == Status::resource_limit && !limited.satisfiable &&
            !limited.numerator && !limited.denominator && !limited.probability,
            "state-space cap must fail before emitting a partial result");

    SolverLimits exact_boundary;
    exact_boundary.max_assignments = 8;
    exact_boundary.max_operations = 32;
    require(solve_finite(task, exact_boundary).status == Status::success,
            "state and operation budgets must allow exact-limit workloads");

    SolverLimits operations;
    operations.max_operations = 2;
    require(solve_finite(task, operations).status == Status::resource_limit,
            "estimated work cap must be enforced");

    SolverLimits expired;
    expired.timeout_ms = 0;
    const auto timed_out = solve_finite(task, expired);
    require(timed_out.status == Status::timeout && !timed_out.satisfiable &&
            !timed_out.numerator && !timed_out.denominator && !timed_out.probability,
            "expired deadline must return timeout without partial output");

    SolverLimits rows;
    rows.max_table_rows = 0;
    task.constraints = {TableConstraint{{"x"}, {{"0"}}}};
    require(solve_finite(task, rows).status == Status::resource_limit,
            "table-row cap must be enforced");
}

void test_causal_fail_closed_policy() {
    const auto association_only = assess_causal_request({});
    require(association_only.status == Status::non_identified,
            "association without a declared SCM must be non-identified");

    const CausalDeclaration complete{true, true, true, true, true};
    const auto declared = assess_causal_request(complete);
    require(declared.status == Status::unsupported_input,
            "declared causal assumptions cannot produce an answer without an estimator");
    require(declared.diagnostic.find("no intervention result") != std::string::npos,
            "causal limitation must explicitly say no intervention result was emitted");
}

}  // namespace

int main() {
    try {
        test_exact_conditional_probability();
        std::cout << "PASS exact_conditional_probability: rational WMC and normalized query\n";
        test_sat_unsat_zero_weight_and_empty_task();
        std::cout << "PASS sat_unsat_zero_weight: structural satisfiability and zero normalizer\n";
        test_query_edges_and_evidence_conflict();
        std::cout << "PASS query_evidence_edges: disjoint query and malformed evidence\n";
        test_exhaustive_three_boolean_relation_grid();
        std::cout << "PASS differential_oracle: all 256 three-variable Boolean relations\n";
        test_malformed_and_unsupported_inputs();
        std::cout << "PASS schema_and_grounding: malformed tables and unsupported grounding\n";
        test_budgets_and_no_partial_result();
        std::cout << "PASS budgets: state, operation, table-row and deadline failures\n";
        test_causal_fail_closed_policy();
        std::cout << "PASS causal_policy: non-identification and unsupported estimator are distinct\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL reasoning_tests: " << error.what() << '\n';
        return 1;
    }
}
