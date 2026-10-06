#include "xai/wmc.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace xai::wmc;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] Rational fraction(unsigned long numerator, unsigned long denominator) {
    Rational value(numerator, denominator);
    value.canonicalize();
    return value;
}

[[nodiscard]] Instance parse(const std::string& text, const ParseLimits& limits = {}) {
    std::istringstream input(text);
    return parse_dimacs_wmc(input, limits);
}

struct OracleResult {
    Rational count{0};
    bool satisfiable = false;
};

[[nodiscard]] OracleResult enumerate(const Instance& instance) {
    if (instance.variables >= 63) throw std::runtime_error("oracle fixture exceeds mask width");
    const std::uint64_t assignments = 1ULL << instance.variables;
    OracleResult result;
    for (std::uint64_t mask = 0; mask < assignments; ++mask) {
        bool satisfies = true;
        for (const auto& clause : instance.clauses) {
            bool clause_true = false;
            for (const auto literal : clause) {
                const auto variable = static_cast<std::size_t>(literal < 0 ? -literal : literal);
                const bool value = ((mask >> (variable - 1)) & 1ULL) != 0;
                if ((literal > 0 && value) || (literal < 0 && !value)) {
                    clause_true = true;
                    break;
                }
            }
            if (!clause_true) {
                satisfies = false;
                break;
            }
        }
        if (!satisfies) continue;
        result.satisfiable = true;
        Rational model_weight{1};
        for (std::size_t variable = 1; variable <= instance.variables; ++variable) {
            const bool value = ((mask >> (variable - 1)) & 1ULL) != 0;
            const auto& pair = instance.weights[variable - 1];
            model_weight *= value ? pair.positive : pair.negative;
        }
        result.count += model_weight;
    }
    return result;
}

void check_against_oracle(const std::string& text, const std::string& label) {
    const auto instance = parse(text);
    const auto expected = enumerate(instance);
    const auto actual = exact_wmc(instance);
    require(actual.count == expected.count, label + ": exact count differs from enumeration");
    require(actual.satisfiable == expected.satisfiable,
            label + ": SAT status differs from independent enumeration");
}

template <typename Exception, typename Function>
void expect_exception(Function&& function, const std::string& label) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    require(threw, label + ": expected exception was not raised");
}

void test_exact_weight_parsing() {
    require(parse_weight("0.1") == fraction(1, 10), "decimal 0.1 must be exact");
    require(parse_weight("1/3") == fraction(1, 3), "fraction 1/3 must be exact");
    require(parse_weight("3e-5") == fraction(3, 100000), "scientific notation must be exact");
    require(parse_weight("1.23e+4") == Rational(12300), "positive exponent parsing mismatch");
    expect_exception<ParseError>([] { (void)parse_weight("1/0"); }, "zero denominator");
    expect_exception<ParseError>([] { (void)parse_weight("NaN"); }, "NaN weight");
    expect_exception<ParseError>([] { (void)parse_weight("1.2.3"); }, "multiple decimal points");
}

void test_counting_and_status() {
    const auto empty = exact_wmc(parse("p cnf 0 0\n"));
    require(empty.count == 1 && empty.satisfiable, "empty formula must have one empty assignment");

    check_against_oracle("p cnf 2 1\n1 2 0\n", "default weights");
    const auto default_count = exact_wmc(parse("p cnf 2 1\n1 2 0\n"));
    require(default_count.count == 3 && default_count.satisfiable,
            "unweighted clause x1 OR x2 must have three models");

    const auto explicit_default = exact_wmc(parse(
        "p cnf 1 0\nc p weight 1 1 0\nc p weight -1 1 0\n"));
    require(explicit_default.count == 2 && explicit_default.satisfiable,
            "explicit (1,1) pair must be accepted as the unweighted default");

    const auto weighted = exact_wmc(parse(
        "c t wmc\np cnf 2 2\nc p weight 1 3/10 0\nc p weight -1 7/10 0\n"
        "c p weight 2 0.6 0\nc p weight -2 0.4 0\n1 2 0\n-1 2 0\n"));
    require(weighted.count == fraction(3, 5), "exact WMC of the two-clause fixture must be 3/5");
    require(weighted.satisfiable, "weighted fixture must be satisfiable");

    const auto unsat = exact_wmc(parse("p cnf 1 2\n1 0\n-1 0\n"));
    require(unsat.count == 0 && !unsat.satisfiable, "contradictory unit clauses must be UNSAT");

    const auto zero_weight = exact_wmc(parse(
        "c t wmc\np cnf 1 1\nc p weight 1 0 0\nc p weight -1 1 0\n1 0\n"));
    require(zero_weight.count == 0, "all satisfying models with zero weight must have WMC zero");
    require(zero_weight.satisfiable,
            "zero WMC due to zero literal weight must not be mislabeled UNSAT");

    const auto empty_clause = exact_wmc(parse("p cnf 1 1\n0\n"));
    require(empty_clause.count == 0 && !empty_clause.satisfiable,
            "an empty clause must be unsatisfiable");
}

void test_components_free_variables_and_normalization() {
    const std::string disconnected = "p cnf 4 2\n1 2 0\n3 4 0\n";
    const auto decomposed = exact_wmc(parse(disconnected));
    require(decomposed.count == 9 && decomposed.satisfiable,
            "two disconnected binary clauses must factor to 9 models");
    require(decomposed.stats.component_splits > 0,
            "component decomposition should record a split");

    const auto free_variable = exact_wmc(parse(
        "c t wmc\np cnf 2 1\nc p weight 1 3/10 0\nc p weight -1 7/10 0\n1 0\n"));
    require(free_variable.count == fraction(3, 5) && free_variable.satisfiable,
            "free default-weight variable must contribute a factor of 2");

    check_against_oracle("p cnf 2 3\n1 -1 0\n2 2 0\n2 0\n", "tautology and duplicate literal");
    const auto tautology = exact_wmc(parse("p cnf 2 2\n1 -1 0\n-2 2 0\n"));
    require(tautology.count == 4 && tautology.satisfiable,
            "tautological clauses must be safely removed");

    const auto multiline = exact_wmc(parse("p cnf 3 2\n1 -2\n0 2 3 0\n"));
    require(multiline.count == 4 && multiline.satisfiable,
            "clauses may span lines and multiple clauses may share a line");
}

void test_ablation_equivalence() {
    const std::string fixture =
        "c t wmc\np cnf 5 3\nc p weight 1 0.2 0\nc p weight -1 0.8 0\n"
        "c p weight 2 0.4 0\nc p weight -2 0.6 0\n"
        "c p weight 3 0.3 0\nc p weight -3 0.7 0\n"
        "1 2 0\n-1 2 0\n4 5 0\n";
    const auto instance = parse(fixture);
    const auto expected = enumerate(instance);
    const std::vector<SolverOptions> modes = [] {
        std::vector<SolverOptions> values(4);
        values[1].unit_propagation = false;
        values[2].component_decomposition = false;
        values[3].occurrence_branching = false;
        return values;
    }();
    for (std::size_t i = 0; i < modes.size(); ++i) {
        const auto actual = exact_wmc(instance, modes[i]);
        require(actual.count == expected.count, "solver ablation changed the exact count");
        require(actual.satisfiable == expected.satisfiable, "solver ablation changed SAT status");
    }
}

void test_strict_input_validation() {
    expect_exception<ParseError>([] { (void)parse("1 0\n"); }, "missing header");
    expect_exception<ParseError>([] { (void)parse("p cnf 1 2\n1 0\n"); }, "clause count mismatch");
    expect_exception<ParseError>([] { (void)parse("p cnf 1 1\n2 0\n"); }, "out-of-range literal");
    expect_exception<ParseError>([] { (void)parse("p cnf 1 1\n1\n"); }, "unterminated clause");
    expect_exception<ParseError>([] { (void)parse("p cnf 1 0\np cnf 1 0\n"); }, "duplicate header");
    expect_exception<UnsupportedInput>([] { (void)parse(
        "c t wmc\np cnf 1 1\nc p weight 1 0.2 0\n1 0\n"); }, "unpaired weight");
    expect_exception<UnsupportedInput>([] { (void)parse(
        "c t wmc\np cnf 1 0\nc p weight 1 0.2 0\nc p weight -1 0.7 0\n"); },
        "non-normalized weights");
    expect_exception<UnsupportedInput>([] { (void)parse(
        "c t wmc\np cnf 1 0\nc p weight 1 0.01784680097703691 0\n"
        "c p weight -1 0.982153199022963 0\n"); },
        "high-precision decimal pair that is not an exact rational complement");
    expect_exception<UnsupportedInput>([] { (void)parse(
        "c t wmc\np cnf 1 0\nc p weight 1 -0.2 0\nc p weight -1 1.2 0\n"); },
        "negative and over-one weights");
    expect_exception<UnsupportedInput>([] { (void)parse("c t pwmc\np cnf 1 0\n"); },
        "projected counting task type");
    expect_exception<ParseError>([] { (void)parse("c t wmc extra\np cnf 1 0\n"); },
        "malformed task type marker");
    expect_exception<UnsupportedInput>([] { (void)parse("p cnf 1 0\nc p show 1 0\n"); },
        "projection directive");
    expect_exception<ParseError>([] { (void)parse(
        "p cnf 1 0\nc p weight 1 1/0 0\n"); }, "zero weight denominator");

    Instance missing_weights;
    missing_weights.variables = 1;
    expect_exception<ParseError>([&] { (void)exact_wmc(missing_weights); },
                                 "typed instance weight-vector mismatch");
    Instance zero_literal;
    zero_literal.variables = 1;
    zero_literal.weights.resize(1);
    zero_literal.clauses = {{0}};
    expect_exception<ParseError>([&] { (void)exact_wmc(zero_literal); },
                                 "typed instance zero literal");
    Instance malformed_weights;
    malformed_weights.variables = 1;
    malformed_weights.weights = {{fraction(1, 2), fraction(1, 4)}};
    expect_exception<UnsupportedInput>([&] { (void)exact_wmc(malformed_weights); },
                                       "typed instance normalization");

    ParseLimits size_limit;
    size_limit.max_variables = 1;
    expect_exception<ResourceLimit>([&] { (void)parse("p cnf 2 0\n", size_limit); },
                                    "declared variable limit");
    ParseLimits byte_limit;
    byte_limit.max_input_bytes = 4;
    expect_exception<ResourceLimit>([&] { (void)parse("p cnf 1 0\n", byte_limit); },
                                    "input byte limit");
}

void test_explicit_solver_limits() {
    const auto instance = parse("p cnf 2 1\n1 2 0\n");
    SolverOptions nodes;
    nodes.node_limit = 1;
    expect_exception<ResourceLimit>([&] { (void)exact_wmc(instance, nodes); },
                                    "recursive node limit");
    SolverOptions depth;
    depth.max_depth = 0;
    expect_exception<ResourceLimit>([&] { (void)exact_wmc(instance, depth); },
                                    "recursion depth limit");

    Instance long_clause;
    long_clause.variables = 1;
    long_clause.weights.resize(1);
    Clause literals;
    literals.reserve(5'000'000);
    literals.insert(literals.end(), 5'000'000, 1);
    long_clause.declared_clauses = 1;
    long_clause.clauses.push_back(std::move(literals));
    SolverOptions timeout;
    timeout.timeout_ms = 1;
    timeout.node_limit = 0;
    expect_exception<ResourceLimit>([&] { (void)exact_wmc(long_clause, timeout); },
                                    "wall-clock timeout");
}

void test_small_independent_oracle_grid() {
    const std::vector<std::string> formulas = {
        "p cnf 1 0\n",
        "p cnf 1 1\n1 0\n",
        "p cnf 1 1\n-1 0\n",
        "p cnf 2 2\n1 2 0\n-1 2 0\n",
        "p cnf 3 3\n1 2 0\n-1 3 0\n-2 -3 0\n",
        "p cnf 3 2\n1 -2 0\n2 -3 0\n",
        "p cnf 3 3\n1 0\n-1 2 0\n-2 3 0\n",
        "p cnf 3 2\n1 2 0\n-1 -2 0\n",
    };
    for (std::size_t i = 0; i < formulas.size(); ++i) {
        const auto instance = parse(formulas[i]);
        const auto expected = enumerate(instance);
        const auto actual = exact_wmc(instance);
        require(actual.count == expected.count,
                "fixed exhaustive oracle grid count mismatch at case " + std::to_string(i));
        require(actual.satisfiable == expected.satisfiable,
                "fixed exhaustive oracle grid SAT mismatch at case " + std::to_string(i));
    }
}

void test_exhaustive_two_variable_formula_grid() {
    const std::vector<Clause> basis = {
        {1}, {-1}, {2}, {-2}, {1, 2}, {1, -2}, {-1, 2}, {-1, -2},
        {1, -1}, {2, -2}, {}};
    Instance empty;
    empty.variables = 2;
    empty.weights = {{fraction(2, 7), fraction(5, 7)},
                     {fraction(3, 8), fraction(5, 8)}};
    const auto check = [](const Instance& instance, std::size_t index) {
        const auto expected = enumerate(instance);
        const auto actual = exact_wmc(instance);
        require(actual.count == expected.count,
                "exhaustive formula grid WMC mismatch at case " + std::to_string(index));
        require(actual.satisfiable == expected.satisfiable,
                "exhaustive formula grid SAT mismatch at case " + std::to_string(index));
    };
    std::size_t case_index = 0;
    check(empty, case_index++);
    for (std::size_t left = 0; left < basis.size(); ++left) {
        Instance single = empty;
        single.clauses = {basis[left]};
        single.declared_clauses = single.clauses.size();
        check(single, case_index++);
        for (std::size_t right = left + 1; right < basis.size(); ++right) {
            Instance pair = empty;
            pair.clauses = {basis[left], basis[right]};
            pair.declared_clauses = pair.clauses.size();
            check(pair, case_index++);
        }
    }
    require(case_index == 67, "formula-grid fixture count changed unexpectedly");
}

}  // namespace

int main() {
    try {
        test_exact_weight_parsing();
        std::cout << "PASS rational_parser: exact fractions, decimals, exponents, malformed tokens\n";
        test_counting_and_status();
        std::cout << "PASS counting_status: exact WMC, SAT/UNSAT, zero-WMC distinction\n";
        test_components_free_variables_and_normalization();
        std::cout << "PASS structure: decomposition, free variables, tautologies, multiline clauses\n";
        test_ablation_equivalence();
        std::cout << "PASS ablations: unit propagation, components, branch heuristic preserve answers\n";
        test_strict_input_validation();
        std::cout << "PASS validation: schema, weights, projection, typed instances, ranges and byte limits\n";
        test_explicit_solver_limits();
        std::cout << "PASS resource_limits: node, depth and wall-time exhaustion raise explicit status\n";
        test_small_independent_oracle_grid();
        std::cout << "PASS oracle_grid: all fixed small formulas agree with exhaustive enumeration\n";
        test_exhaustive_two_variable_formula_grid();
        std::cout << "PASS exhaustive_grid: 67 weighted formulas agree with the independent oracle\n";
        std::cout << "RESULT: 8/8 WMC test groups passed; no performance or corpus result is implied.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
