#include "xai/reasoning.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xai::reasoning {
namespace {

using Clock = std::chrono::steady_clock;

struct CompiledVariable {
    std::string id;
    std::vector<std::string> domain;
    std::map<std::string, std::size_t, std::less<>> value_indices;
    std::vector<Rational> weights;
};

struct CompiledConstraint {
    std::vector<std::size_t> scope;
    std::set<std::vector<std::size_t>> allowed_tuples;
};

using CompiledEvent = std::vector<std::pair<std::size_t, std::size_t>>;

[[nodiscard]] SolveResult failure(contracts::Status status, std::string message,
                                  SolverStats stats = {}) {
    SolveResult result;
    result.status = status;
    result.stats = stats;
    result.diagnostic = std::move(message);
    return result;
}

[[nodiscard]] CompiledEvent compile_event(
    const Assignment& event,
    const std::map<std::string, std::size_t, std::less<>>& variable_indices,
    const std::vector<CompiledVariable>& variables) {
    CompiledEvent compiled;
    compiled.reserve(event.size());
    for (const auto& [id, value] : event) {
        const auto variable = variable_indices.at(id);
        compiled.emplace_back(variable, variables[variable].value_indices.at(value));
    }
    return compiled;
}

}  // namespace

SolveResult solve_finite(const FiniteTask& task, const SolverLimits& limits) {
    const auto start = Clock::now();
    SolverStats stats;
    const auto elapsed_ms = [&]() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - start).count());
    };
    const auto timeout = [&]() {
        stats.elapsed_ms = elapsed_ms();
        return failure(contracts::Status::timeout, "finite-state solver deadline exceeded", stats);
    };
    const auto check_timeout = [&]() {
        return limits.timeout_ms == 0 || elapsed_ms() >= limits.timeout_ms;
    };
    std::uint64_t validation_work = 0;
    const auto check_validation_deadline = [&]() {
        ++validation_work;
        return validation_work % 128 == 0 && check_timeout();
    };
    if (check_timeout()) return timeout();

    if (task.variables.size() > limits.max_variables) {
        return failure(contracts::Status::resource_limit, "variable limit exceeded");
    }
    if (task.constraints.size() > limits.max_constraints) {
        return failure(contracts::Status::resource_limit, "constraint limit exceeded");
    }

    std::vector<CompiledVariable> variables;
    variables.reserve(task.variables.size());
    std::map<std::string, std::size_t, std::less<>> variable_indices;
    std::size_t total_domain_values = 0;
    for (const auto& variable : task.variables) {
        if (check_validation_deadline()) return timeout();
        if (!contracts::valid_identifier(variable.id) || variable.domain.empty()) {
            return failure(contracts::Status::invalid_schema,
                           "each variable needs a valid typed identifier and finite domain");
        }
        if (variable.id.size() > limits.max_symbol_bytes) {
            return failure(contracts::Status::resource_limit, "variable identifier byte limit exceeded");
        }
        if (variable.domain.size() > limits.max_domain_values - total_domain_values) {
            return failure(contracts::Status::resource_limit, "domain-value limit exceeded");
        }
        total_domain_values += variable.domain.size();
        if (!variable_indices.emplace(variable.id, variables.size()).second) {
            return failure(contracts::Status::invalid_schema,
                           "duplicate variable identifier: " + variable.id);
        }
        if (!variable.weights.empty() && variable.weights.size() != variable.domain.size()) {
            return failure(contracts::Status::invalid_schema,
                           "weight count must be zero or equal to domain size for " + variable.id);
        }
        CompiledVariable compiled;
        compiled.id = variable.id;
        compiled.domain = variable.domain;
        compiled.weights = variable.weights;
        if (compiled.weights.empty()) compiled.weights.assign(variable.domain.size(), Rational{1});
        for (std::size_t value = 0; value < variable.domain.size(); ++value) {
            if (check_validation_deadline()) return timeout();
            const auto& symbol = variable.domain[value];
            if (symbol.size() > limits.max_symbol_bytes) {
                return failure(contracts::Status::resource_limit, "domain symbol byte limit exceeded");
            }
            if (symbol.empty() || !compiled.value_indices.emplace(symbol, value).second) {
                return failure(contracts::Status::invalid_schema,
                               "domain values must be non-empty and unique for " + variable.id);
            }
            if (compiled.weights[value] < 0) {
                return failure(contracts::Status::invalid_schema,
                               "weights must be non-negative for " + variable.id);
            }
            compiled.weights[value].canonicalize();
        }
        variables.push_back(std::move(compiled));
    }

    const auto resolve_assignment = [&](const Assignment& assignment,
        const char* label) -> std::optional<std::pair<contracts::Status, std::string>> {
        for (const auto& [id, value] : assignment) {
            if (check_validation_deadline()) {
                return std::pair{contracts::Status::timeout,
                                 std::string("finite-state solver deadline exceeded")};
            }
            if (id.size() > limits.max_symbol_bytes || value.size() > limits.max_symbol_bytes) {
                return std::pair{contracts::Status::resource_limit,
                                 std::string(label) + " symbol exceeds byte limit"};
            }
            const auto variable = variable_indices.find(id);
            if (variable == variable_indices.end()) {
                return std::pair{contracts::Status::invalid_schema,
                                 std::string(label) + " references undeclared variable: " + id};
            }
            if (!variables[variable->second].value_indices.contains(value)) {
                return std::pair{contracts::Status::invalid_schema,
                                 std::string(label) + " uses value outside domain for " + id};
            }
        }
        return std::nullopt;
    };
    if (const auto error = resolve_assignment(task.evidence, "evidence")) {
        return failure(error->first, error->second);
    }
    if (const auto error = resolve_assignment(task.query, "query")) {
        return failure(error->first, error->second);
    }
    const auto evidence = compile_event(task.evidence, variable_indices, variables);
    const auto query = compile_event(task.query, variable_indices, variables);

    std::vector<CompiledConstraint> constraints;
    constraints.reserve(task.constraints.size());
    std::size_t total_rows = 0;
    std::size_t total_cells = 0;
    for (const auto& item : task.constraints) {
        if (check_validation_deadline()) return timeout();
        if (const auto* unsupported = std::get_if<UnsupportedConstraint>(&item)) {
            const auto feature = unsupported->feature.empty() ? "unspecified grounding" : unsupported->feature;
            return failure(contracts::Status::unsupported_input,
                           "unsupported constraint grounding: " + feature);
        }
        const auto& table = std::get<TableConstraint>(item);
        if (table.scope.empty()) {
            return failure(contracts::Status::invalid_schema,
                           "table constraint scope must not be empty");
        }
        if (table.scope.size() > limits.max_variables) {
            return failure(contracts::Status::resource_limit, "constraint-scope limit exceeded");
        }
        if (table.allowed_tuples.size() > limits.max_table_rows - total_rows) {
            return failure(contracts::Status::resource_limit, "table-row limit exceeded");
        }
        if (table.allowed_tuples.size() >
            (limits.max_table_cells - total_cells) / table.scope.size()) {
            return failure(contracts::Status::resource_limit, "table-cell limit exceeded");
        }
        total_rows += table.allowed_tuples.size();
        total_cells += table.allowed_tuples.size() * table.scope.size();
        CompiledConstraint compiled;
        std::set<std::size_t> seen_scope;
        for (const auto& id : table.scope) {
            if (check_validation_deadline()) return timeout();
            if (id.size() > limits.max_symbol_bytes) {
                return failure(contracts::Status::resource_limit, "constraint symbol byte limit exceeded");
            }
            const auto variable = variable_indices.find(id);
            if (variable == variable_indices.end()) {
                return failure(contracts::Status::invalid_schema,
                               "constraint scope references undeclared variable: " + id);
            }
            if (!seen_scope.insert(variable->second).second) {
                return failure(contracts::Status::invalid_schema,
                               "constraint scope contains a duplicate variable: " + id);
            }
            compiled.scope.push_back(variable->second);
        }
        for (const auto& tuple : table.allowed_tuples) {
            if (check_validation_deadline()) return timeout();
            if (tuple.size() != compiled.scope.size()) {
                return failure(contracts::Status::invalid_schema,
                               "allowed tuple arity does not match constraint scope");
            }
            std::vector<std::size_t> encoded;
            encoded.reserve(tuple.size());
            for (std::size_t position = 0; position < tuple.size(); ++position) {
                if (check_validation_deadline()) return timeout();
                if (tuple[position].size() > limits.max_symbol_bytes) {
                    return failure(contracts::Status::resource_limit, "tuple symbol byte limit exceeded");
                }
                const auto variable = compiled.scope[position];
                const auto value = variables[variable].value_indices.find(tuple[position]);
                if (value == variables[variable].value_indices.end()) {
                    return failure(contracts::Status::invalid_schema,
                                   "allowed tuple contains a value outside its variable domain");
                }
                encoded.push_back(value->second);
            }
            if (!compiled.allowed_tuples.insert(std::move(encoded)).second) {
                return failure(contracts::Status::invalid_schema,
                               "duplicate allowed tuple in table constraint");
            }
        }
        constraints.push_back(std::move(compiled));
    }
    if (check_timeout()) return timeout();

    std::uint64_t state_count = 1;
    for (const auto& variable : variables) {
        const auto domain_size = static_cast<std::uint64_t>(variable.domain.size());
        if (domain_size == 0 || state_count > limits.max_assignments / domain_size) {
            return failure(contracts::Status::resource_limit,
                           "finite Cartesian state space exceeds assignment limit");
        }
        state_count *= domain_size;
    }
    const std::uint64_t operations_per_assignment =
        static_cast<std::uint64_t>(variables.size()) +
        static_cast<std::uint64_t>(constraints.size()) + 1 +
        static_cast<std::uint64_t>(evidence.size()) +
        static_cast<std::uint64_t>(query.size());
    if (operations_per_assignment == 0 ||
        state_count > limits.max_operations / operations_per_assignment) {
        return failure(contracts::Status::resource_limit,
                       "estimated enumeration work exceeds operation limit");
    }

    if (check_timeout()) return timeout();

    Rational numerator{0};
    Rational denominator{0};
    bool satisfiable = false;
    std::vector<std::size_t> values(variables.size(), 0);
    std::uint64_t work_since_clock_check = 0;
    const auto tick = [&]() {
        ++stats.operations;
        ++work_since_clock_check;
        if (work_since_clock_check >= 128) work_since_clock_check = 0;
        return work_since_clock_check == 0 && check_timeout();
    };
    for (std::uint64_t state = 0; state < state_count; ++state) {
        if (work_since_clock_check == 0 && check_timeout()) return timeout();
        ++stats.assignments_examined;
        if (tick()) return timeout();
        bool satisfies_evidence = true;
        for (const auto& [variable, expected] : evidence) {
            if (tick()) return timeout();
            if (values[variable] != expected) {
                satisfies_evidence = false;
                break;
            }
        }
        bool satisfies_constraints = satisfies_evidence;
        if (satisfies_constraints) {
            for (const auto& constraint : constraints) {
                std::vector<std::size_t> tuple;
                tuple.reserve(constraint.scope.size());
                for (const auto variable : constraint.scope) tuple.push_back(values[variable]);
                ++stats.constraint_checks;
                if (tick()) return timeout();
                if (constraint.allowed_tuples.find(tuple) == constraint.allowed_tuples.end()) {
                    satisfies_constraints = false;
                    break;
                }
            }
        }
        if (satisfies_constraints) {
            satisfiable = true;
            Rational mass{1};
            for (std::size_t variable = 0; variable < variables.size(); ++variable) {
                mass *= variables[variable].weights[values[variable]];
                if (tick()) return timeout();
            }
            denominator += mass;
            bool satisfies_query = true;
            for (const auto& [variable, expected] : query) {
                if (tick()) return timeout();
                if (values[variable] != expected) {
                    satisfies_query = false;
                    break;
                }
            }
            if (satisfies_query) numerator += mass;
        }
        for (std::size_t variable = values.size(); variable > 0; --variable) {
            const auto index = variable - 1;
            ++values[index];
            if (values[index] < variables[index].domain.size()) break;
            values[index] = 0;
        }
    }
    stats.elapsed_ms = elapsed_ms();
    if (check_timeout()) return timeout();

    SolveResult result;
    result.satisfiable = satisfiable;
    result.numerator = numerator;
    result.denominator = denominator;
    result.stats = stats;
    if (!satisfiable) {
        result.status = contracts::Status::inconsistent;
        result.diagnostic = "hard constraints and evidence admit no finite assignment";
        return result;
    }
    if (denominator == 0) {
        result.status = contracts::Status::zero_normalizer;
        result.diagnostic = "satisfying assignments have zero total weight";
        return result;
    }
    Rational probability = numerator / denominator;
    probability.canonicalize();
    result.status = contracts::Status::success;
    result.probability = std::move(probability);
    return result;
}

CausalPolicyResult assess_causal_request(const CausalDeclaration& declaration) {
    if (!declaration.scm_declared || !declaration.graph_declared ||
        !declaration.mechanisms_declared ||
        !declaration.identification_conditions_declared ||
        !declaration.identification_conditions_satisfied) {
        return {contracts::Status::non_identified,
                "no intervention result: a declared SCM, graph, mechanisms, and satisfied "
                "identification conditions are required; association alone is insufficient"};
    }
    return {contracts::Status::unsupported_input,
            "SCM and identification declarations are present, but this phase implements no "
            "causal estimator or identification verifier; no intervention result was emitted"};
}

}  // namespace xai::reasoning
