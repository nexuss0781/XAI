#include "xai/wmc.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace xai::wmc {
namespace {

constexpr std::size_t kMaxWeightText = 128;
constexpr long kMaxPowerOfTen = 2000;
constexpr std::uint64_t kBudgetCheckPeriod = 1024;
constexpr std::uint64_t kMaxTimeoutMs = 1'000'000'000'000ULL;
using Assignment = std::vector<std::int8_t>;
using Clock = std::chrono::steady_clock;

[[nodiscard]] std::vector<std::string> split_fields(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) fields.push_back(field);
    return fields;
}

template <typename Integer>
[[nodiscard]] Integer parse_integer(const std::string& token, const char* name) {
    Integer value{};
    const char* begin = token.data();
    const char* end = begin + token.size();
    const auto [position, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || position != end) {
        throw ParseError(std::string("invalid ") + name + ": " + token);
    }
    return value;
}

[[nodiscard]] mpz_class parse_integer_z(std::string_view digits) {
    if (digits.empty()) throw ParseError("empty integer in weight");
    const std::string owned(digits);
    mpz_class result;
    if (mpz_set_str(result.get_mpz_t(), owned.c_str(), 10) != 0) {
        throw ParseError("invalid integer in weight: " + owned);
    }
    return result;
}

[[nodiscard]] mpz_class power_of_ten(unsigned long exponent) {
    mpz_class result;
    mpz_ui_pow_ui(result.get_mpz_t(), 10, exponent);
    return result;
}

[[nodiscard]] std::size_t variable_of(Literal literal) {
    const auto wide = static_cast<std::int64_t>(literal);
    return static_cast<std::size_t>(wide < 0 ? -wide : wide);
}

[[nodiscard]] Rational literal_weight(const Instance& instance, Literal literal) {
    const auto& pair = instance.weights.at(variable_of(literal) - 1);
    return literal > 0 ? pair.positive : pair.negative;
}

[[nodiscard]] bool literal_true(Literal literal, std::int8_t value) {
    return literal > 0 ? value > 0 : value < 0;
}

struct PartialResult {
    Rational count{0};
    bool satisfiable = false;
};

class DisjointSet {
public:
    explicit DisjointSet(std::size_t size) : parent_(size), rank_(size, 0) {
        for (std::size_t i = 0; i < size; ++i) parent_[i] = i;
    }

    [[nodiscard]] std::size_t find(std::size_t item) {
        if (parent_[item] != item) parent_[item] = find(parent_[item]);
        return parent_[item];
    }

    void unite(std::size_t left, std::size_t right) {
        auto a = find(left);
        auto b = find(right);
        if (a == b) return;
        if (rank_[a] < rank_[b]) std::swap(a, b);
        parent_[b] = a;
        if (rank_[a] == rank_[b]) ++rank_[a];
    }

private:
    std::vector<std::size_t> parent_;
    std::vector<std::uint8_t> rank_;
};

class AssignmentTrail {
public:
    explicit AssignmentTrail(Assignment& assignment) : assignment_(assignment) {}
    AssignmentTrail(const AssignmentTrail&) = delete;
    AssignmentTrail& operator=(const AssignmentTrail&) = delete;

    ~AssignmentTrail() {
        for (auto entry = changes_.rbegin(); entry != changes_.rend(); ++entry) {
            assignment_[entry->first] = entry->second;
        }
    }

    void assign(std::size_t variable, std::int8_t value) {
        changes_.emplace_back(variable, assignment_[variable]);
        assignment_[variable] = value;
    }

private:
    Assignment& assignment_;
    std::vector<std::pair<std::size_t, std::int8_t>> changes_;
};

class ExactSolver {
public:
    ExactSolver(const Instance& instance, SolverOptions options)
        : instance_(instance), options_(options), start_(Clock::now()) {}

    [[nodiscard]] SolveResult run() {
        std::vector<std::size_t> scope(instance_.variables);
        for (std::size_t i = 0; i < scope.size(); ++i) scope[i] = i + 1;
        Assignment assignment(instance_.variables + 1, 0);
        const auto partial = recurse(instance_.clauses, assignment, scope, 0);
        stats_.elapsed_ms = elapsed_ms();
        return {partial.count, partial.satisfiable, stats_};
    }

private:
    const Instance& instance_;
    SolverOptions options_;
    Clock::time_point start_;
    SolverStats stats_;
    std::uint64_t operations_since_check_ = 0;

    [[nodiscard]] std::uint64_t elapsed_ms() const {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - start_);
        return static_cast<std::uint64_t>(elapsed.count());
    }

    void check_budget(bool force = false) {
        if (force || ++operations_since_check_ >= kBudgetCheckPeriod) {
            operations_since_check_ = 0;
            if (options_.node_limit != 0 && stats_.recursive_nodes > options_.node_limit) {
                throw ResourceLimit("recursive node limit exceeded");
            }
            if (options_.timeout_ms != 0) {
                const auto elapsed = std::chrono::duration<long double, std::milli>(
                    Clock::now() - start_).count();
                if (elapsed > static_cast<long double>(options_.timeout_ms)) {
                    throw ResourceLimit("time limit exceeded");
                }
            }
        }
    }

    [[nodiscard]] Rational free_weight(const std::vector<std::size_t>& scope,
                                      const Assignment& assignment,
                                      const std::vector<bool>* active = nullptr) const {
        Rational product{1};
        for (const auto variable : scope) {
            if (assignment[variable] != 0 || (active != nullptr && (*active)[variable])) {
                continue;
            }
            const auto& pair = instance_.weights[variable - 1];
            product *= pair.positive + pair.negative;
        }
        return product;
    }

    [[nodiscard]] std::size_t choose_variable(const std::vector<Clause>& clauses,
                                              const Assignment& assignment) const {
        if (!options_.occurrence_branching) {
            for (const auto& clause : clauses) {
                for (const auto literal : clause) {
                    const auto variable = variable_of(literal);
                    if (assignment[variable] == 0) return variable;
                }
            }
        }
        std::unordered_map<std::size_t, std::size_t> occurrences;
        for (const auto& clause : clauses) {
            for (const auto literal : clause) {
                const auto variable = variable_of(literal);
                if (assignment[variable] == 0) ++occurrences[variable];
            }
        }
        if (occurrences.empty()) throw ParseError("internal error: no branch variable");
        std::size_t best_variable = 0;
        std::size_t best_count = 0;
        for (const auto& [variable, count] : occurrences) {
            if (count > best_count || (count == best_count && variable < best_variable)) {
                best_variable = variable;
                best_count = count;
            }
        }
        return best_variable;
    }

    struct Component {
        std::vector<Clause> clauses;
        std::vector<std::size_t> variables;
    };

    struct SplitResult {
        bool factorable = false;
        Rational free_factor{1};
        std::vector<Component> components;
    };

    [[nodiscard]] SplitResult split_components(const std::vector<Clause>& clauses,
                                                const std::vector<std::size_t>& scope,
                                                const Assignment& assignment) {
        if (clauses.empty()) return {};
        DisjointSet sets(clauses.size());
        std::unordered_map<std::size_t, std::size_t> first_clause_for_variable;
        first_clause_for_variable.reserve(clauses.size() * 2);
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            for (const auto literal : clauses[i]) {
                const auto variable = variable_of(literal);
                const auto [found, inserted] = first_clause_for_variable.emplace(variable, i);
                if (!inserted) sets.unite(i, found->second);
            }
        }

        std::map<std::size_t, Component> groups;
        std::vector<bool> active(instance_.variables + 1, false);
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            const auto root = sets.find(i);
            auto& component = groups[root];
            component.clauses.push_back(clauses[i]);
            for (const auto literal : clauses[i]) {
                const auto variable = variable_of(literal);
                if (!active[variable]) {
                    active[variable] = true;
                    component.variables.push_back(variable);
                }
            }
        }
        const bool has_free = std::any_of(scope.begin(), scope.end(), [&](std::size_t variable) {
            return assignment[variable] == 0 && !active[variable];
        });
        if (groups.size() < 2 && !has_free) return {};

        SplitResult result;
        result.factorable = true;
        result.free_factor = free_weight(scope, assignment, &active);
        result.components.reserve(groups.size());
        for (auto& [root, component] : groups) {
            (void)root;
            result.components.push_back(std::move(component));
        }
        if (result.components.size() > 1) ++stats_.component_splits;
        return result;
    }

    [[nodiscard]] PartialResult recurse(std::vector<Clause> clauses,
                                        Assignment& assignment,
                                        const std::vector<std::size_t>& scope,
                                        std::size_t depth) {
        AssignmentTrail local_trail(assignment);
        ++stats_.recursive_nodes;
        check_budget(true);
        stats_.maximum_depth = std::max(stats_.maximum_depth, depth);
        if (depth > options_.max_depth) throw ResourceLimit("maximum recursion depth exceeded");

        Rational forced_weight{1};
        for (;;) {
            std::vector<Clause> residual;
            residual.reserve(clauses.size());
            bool propagated = false;
            for (std::size_t clause_index = 0; clause_index < clauses.size(); ++clause_index) {
                const auto& clause = clauses[clause_index];
                check_budget();
                bool satisfied = false;
                Literal unit = 0;
                std::size_t unassigned = 0;
                Clause remaining;
                remaining.reserve(clause.size());
                for (const auto literal : clause) {
                    check_budget();
                    const auto value = assignment[variable_of(literal)];
                    if (value == 0) {
                        unit = literal;
                        ++unassigned;
                        remaining.push_back(literal);
                    } else if (literal_true(literal, value)) {
                        satisfied = true;
                        break;
                    }
                }
                if (satisfied) continue;
                if (unassigned == 0) return {Rational{0}, false};
                if (options_.unit_propagation && unassigned == 1) {
                    const auto variable = variable_of(unit);
                    local_trail.assign(variable, unit > 0 ? 1 : -1);
                    forced_weight *= literal_weight(instance_, unit);
                    ++stats_.propagations;
                    for (std::size_t rest = clause_index + 1; rest < clauses.size(); ++rest) {
                        residual.push_back(clauses[rest]);
                    }
                    clauses = std::move(residual);
                    propagated = true;
                    break;
                }
                residual.push_back(std::move(remaining));
            }
            if (propagated) continue;
            clauses = std::move(residual);
            break;
        }

        if (clauses.empty()) return {forced_weight * free_weight(scope, assignment), true};

        if (options_.component_decomposition) {
            auto split = split_components(clauses, scope, assignment);
            if (split.factorable) {
                Rational count = forced_weight * split.free_factor;
                bool satisfiable = true;
                for (auto& component : split.components) {
                    const auto part = recurse(std::move(component.clauses), assignment,
                                              component.variables, depth + 1);
                    if (!part.satisfiable) return {Rational{0}, false};
                    count *= part.count;
                }
                return {count, satisfiable};
            }
        }

        const auto variable = choose_variable(clauses, assignment);
        ++stats_.decisions;
        Rational count{0};
        bool satisfiable = false;
        for (int truth = 0; truth <= 1; ++truth) {
            AssignmentTrail branch_trail(assignment);
            branch_trail.assign(variable, truth == 1 ? 1 : -1);
            const auto literal = truth == 1 ? static_cast<Literal>(variable)
                                            : -static_cast<Literal>(variable);
            const auto child = recurse(clauses, assignment, scope, depth + 1);
            count += literal_weight(instance_, literal) * child.count;
            satisfiable = satisfiable || child.satisfiable;
        }
        return {forced_weight * count, satisfiable};
    }
};

}  // namespace

Rational parse_weight(const std::string& text) {
    if (text.empty() || text.size() > kMaxWeightText) {
        throw ParseError("weight token is empty or exceeds the supported length");
    }
    const auto slash = text.find('/');
    if (slash != std::string::npos) {
        if (text.find('/', slash + 1) != std::string::npos) {
            throw ParseError("weight fraction contains multiple slashes");
        }
        const auto numerator = parse_integer_z(std::string_view(text).substr(0, slash));
        const auto denominator = parse_integer_z(std::string_view(text).substr(slash + 1));
        if (denominator == 0) throw ParseError("weight denominator is zero");
        Rational value(numerator, denominator);
        value.canonicalize();
        return value;
    }

    std::string_view mantissa(text);
    long exponent = 0;
    const auto exponent_position = mantissa.find_first_of("eE");
    if (exponent_position != std::string_view::npos) {
        std::string exponent_text(mantissa.substr(exponent_position + 1));
        if (!exponent_text.empty() && exponent_text.front() == '+') {
            exponent_text.erase(exponent_text.begin());
        }
        exponent = parse_integer<long>(exponent_text, "weight exponent");
        mantissa = mantissa.substr(0, exponent_position);
    }
    if (exponent < -kMaxPowerOfTen || exponent > kMaxPowerOfTen) {
        throw ParseError("weight exponent exceeds supported exact-arithmetic bound");
    }
    bool negative = false;
    if (!mantissa.empty() && (mantissa.front() == '-' || mantissa.front() == '+')) {
        negative = mantissa.front() == '-';
        mantissa.remove_prefix(1);
    }
    if (mantissa.empty()) throw ParseError("weight has no digits");
    const auto point = mantissa.find('.');
    if (point != std::string_view::npos && mantissa.find('.', point + 1) != std::string_view::npos) {
        throw ParseError("weight has multiple decimal points");
    }
    std::string digits;
    std::size_t fractional_digits = 0;
    for (std::size_t i = 0; i < mantissa.size(); ++i) {
        const char character = mantissa[i];
        if (character == '.') {
            fractional_digits = mantissa.size() - i - 1;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            throw ParseError("invalid decimal weight: " + text);
        }
        digits.push_back(character);
    }
    if (digits.empty()) throw ParseError("weight has no digits");
    mpz_class numerator = parse_integer_z(digits);
    if (negative) numerator = -numerator;
    const long scale = static_cast<long>(fractional_digits) - exponent;
    if (std::abs(scale) > kMaxPowerOfTen) {
        throw ParseError("weight decimal scale exceeds supported exact-arithmetic bound");
    }
    mpz_class denominator{1};
    if (scale > 0) {
        denominator = power_of_ten(static_cast<unsigned long>(scale));
    } else if (scale < 0) {
        numerator *= power_of_ten(static_cast<unsigned long>(-scale));
    }
    Rational value(numerator, denominator);
    value.canonicalize();
    return value;
}

Instance parse_dimacs_wmc(std::istream& input, const ParseLimits& limits) {
    Instance instance;
    std::map<Literal, Rational> explicit_weights;
    std::vector<Literal> pending_clause;
    std::size_t expected_clauses = 0;
    std::size_t parsed_clause_count = 0;
    std::uint64_t total_literals = 0;
    std::uint64_t total_bytes = 0;
    bool saw_header = false;
    bool saw_type = false;
    std::string task_type;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        if (line.size() > limits.max_line_bytes) {
            throw ResourceLimit("input line exceeds parser byte limit");
        }
        const auto line_bytes = static_cast<std::uint64_t>(line.size()) + 1ULL;
        if (line_bytes > limits.max_input_bytes ||
            total_bytes > limits.max_input_bytes - line_bytes) {
            throw ResourceLimit("input exceeds parser byte limit");
        }
        total_bytes += line_bytes;
        const auto fields = split_fields(line);
        if (fields.empty()) continue;
        if (fields[0] == "c") {
            if (fields.size() >= 2 && fields[1] == "t") {
                if (fields.size() != 3) throw ParseError("malformed problem type directive");
                if (fields[2] != "wmc") {
                    throw UnsupportedInput("only unprojected weighted model counting is supported");
                }
                if (saw_type && task_type != fields[2]) {
                    throw ParseError("conflicting problem type directives");
                }
                saw_type = true;
                task_type = fields[2];
            } else if (fields.size() >= 2 && fields[1] == "p") {
                if (fields.size() < 3) throw ParseError("incomplete problem-specific directive");
                if (fields[2] != "weight") {
                    throw UnsupportedInput("unsupported problem-specific directive: " + fields[2]);
                }
                if (fields.size() != 6 || fields[5] != "0") {
                    throw ParseError("malformed literal weight directive on line " +
                                     std::to_string(line_number));
                }
                const auto literal64 = parse_integer<std::int64_t>(fields[3], "weight literal");
                if (literal64 == 0 || literal64 < std::numeric_limits<Literal>::min() ||
                    literal64 > std::numeric_limits<Literal>::max()) {
                    throw ParseError("weight literal is zero or outside supported range");
                }
                const auto literal = static_cast<Literal>(literal64);
                if (explicit_weights.contains(literal)) {
                    throw ParseError("duplicate weight for literal " + std::to_string(literal));
                }
                explicit_weights.emplace(literal, parse_weight(fields[4]));
            }
            continue;
        }
        if (fields[0] == "p") {
            if (saw_header || fields.size() != 4 || fields[1] != "cnf") {
                throw ParseError("malformed or duplicate problem header on line " +
                                 std::to_string(line_number));
            }
            const auto variables = parse_integer<std::uint64_t>(fields[2], "variable count");
            const auto clauses = parse_integer<std::uint64_t>(fields[3], "clause count");
            if (variables > limits.max_variables || clauses > limits.max_clauses) {
                throw ResourceLimit("declared problem size exceeds parser limits");
            }
            if (variables > static_cast<std::uint64_t>(std::numeric_limits<Literal>::max())) {
                throw ResourceLimit("variable identifiers exceed supported literal width");
            }
            instance.variables = static_cast<std::size_t>(variables);
            expected_clauses = static_cast<std::size_t>(clauses);
            instance.declared_clauses = expected_clauses;
            instance.clauses.reserve(expected_clauses);
            saw_header = true;
            continue;
        }
        if (!saw_header) throw ParseError("clause data before p cnf header");
        for (const auto& token : fields) {
            const auto literal64 = parse_integer<std::int64_t>(token, "clause literal");
            if (literal64 != 0 &&
                (literal64 < -static_cast<std::int64_t>(instance.variables) ||
                 literal64 > static_cast<std::int64_t>(instance.variables) ||
                 literal64 < std::numeric_limits<Literal>::min() ||
                 literal64 > std::numeric_limits<Literal>::max())) {
                throw ParseError("clause literal outside declared variable range");
            }
            if (literal64 == 0) {
                if (parsed_clause_count >= limits.max_clauses) {
                    throw ResourceLimit("clause count exceeds parser limit");
                }
                std::sort(pending_clause.begin(), pending_clause.end(), [](Literal left, Literal right) {
                    const auto left_variable = variable_of(left);
                    const auto right_variable = variable_of(right);
                    return left_variable == right_variable ? left > right
                                                           : left_variable < right_variable;
                });
                pending_clause.erase(std::unique(pending_clause.begin(), pending_clause.end()),
                                     pending_clause.end());
                bool tautology = false;
                for (std::size_t i = 1; i < pending_clause.size(); ++i) {
                    if (static_cast<std::int64_t>(pending_clause[i]) ==
                        -static_cast<std::int64_t>(pending_clause[i - 1])) {
                        tautology = true;
                        break;
                    }
                }
                if (!tautology) instance.clauses.push_back(pending_clause);
                pending_clause.clear();
                ++parsed_clause_count;
                continue;
            }
            pending_clause.push_back(static_cast<Literal>(literal64));
            ++total_literals;
            if (total_literals > limits.max_literals) {
                throw ResourceLimit("literal count exceeds parser limit");
            }
        }
    }
    if (input.bad()) throw ParseError("I/O error while reading input");
    if (!saw_header) throw ParseError("missing p cnf header");
    if (!pending_clause.empty()) throw ParseError("unterminated clause at end of input");
    if (parsed_clause_count != expected_clauses) {
        throw ParseError("declared and parsed clause counts do not match");
    }

    instance.weights.assign(instance.variables, LiteralWeights{});
    for (const auto& [literal, weight] : explicit_weights) {
        const auto variable = variable_of(literal);
        if (variable == 0 || variable > instance.variables) {
            throw ParseError("weight literal outside declared variable range");
        }
        if (weight < 0 || weight > 1) throw UnsupportedInput("weights must lie in [0,1]");
    }
    for (std::size_t variable = 1; variable <= instance.variables; ++variable) {
        const auto positive = explicit_weights.find(static_cast<Literal>(variable));
        const auto negative = explicit_weights.find(-static_cast<Literal>(variable));
        const bool has_positive = positive != explicit_weights.end();
        const bool has_negative = negative != explicit_weights.end();
        if (has_positive != has_negative) {
            throw UnsupportedInput("both literal weights must be supplied for each weighted variable");
        }
        if (!has_positive) continue;
        const bool explicit_unweighted_default =
            positive->second == 1 && negative->second == 1;
        if (positive->second + negative->second != 1 && !explicit_unweighted_default) {
            throw UnsupportedInput("complementary literal weights must sum exactly to one");
        }
        instance.weights[variable - 1] = {positive->second, negative->second};
    }
    return instance;
}

SolveResult exact_wmc(const Instance& instance, const SolverOptions& options) {
    constexpr std::size_t kHardDepthLimit = 4096;
    if (instance.variables > static_cast<std::size_t>(std::numeric_limits<Literal>::max())) {
        throw ParseError("typed instance variable count exceeds literal width");
    }
    if (instance.weights.size() != instance.variables) {
        throw ParseError("typed instance weight vector does not match variable count");
    }
    for (const auto& pair : instance.weights) {
        if (pair.positive < 0 || pair.negative < 0 ||
            pair.positive > 1 || pair.negative > 1 ||
            (pair.positive + pair.negative != 1 &&
             !(pair.positive == 1 && pair.negative == 1))) {
            throw UnsupportedInput("typed instance has unsupported literal weights");
        }
    }
    for (const auto& clause : instance.clauses) {
        for (const auto literal : clause) {
            if (literal == 0 || variable_of(literal) > instance.variables) {
                throw ParseError("typed instance contains an invalid clause literal");
            }
        }
    }
    if (options.max_depth > kHardDepthLimit) {
        throw ResourceLimit("configured recursion depth exceeds the hard safety limit");
    }
    if (options.timeout_ms > kMaxTimeoutMs) {
        throw ResourceLimit("configured timeout exceeds the safe chrono range");
    }
    ExactSolver solver(instance, options);
    return solver.run();
}

}  // namespace xai::wmc
