#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <vector>

#include <gmpxx.h>

namespace xai::wmc {

using Literal = std::int32_t;
using Clause = std::vector<Literal>;
using Rational = mpq_class;

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class UnsupportedInput : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ResourceLimit : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ParseLimits {
    std::size_t max_variables = 1'000'000;
    std::size_t max_clauses = 10'000'000;
    std::uint64_t max_literals = 100'000'000;
    std::size_t max_line_bytes = 16 * 1024 * 1024;
    std::uint64_t max_input_bytes = 4ULL * 1024 * 1024 * 1024;
};

struct LiteralWeights {
    Rational positive{1};
    Rational negative{1};
};

struct Instance {
    std::size_t variables = 0;
    std::size_t declared_clauses = 0;
    std::vector<Clause> clauses;
    std::vector<LiteralWeights> weights;
};

struct SolverOptions {
    std::uint64_t node_limit = 10'000'000;
    std::uint64_t timeout_ms = 600'000;
    std::size_t max_depth = 4096;
    bool unit_propagation = true;
    bool component_decomposition = true;
    bool occurrence_branching = true;
};

struct SolverStats {
    std::uint64_t recursive_nodes = 0;
    std::uint64_t decisions = 0;
    std::uint64_t propagations = 0;
    std::uint64_t component_splits = 0;
    std::size_t maximum_depth = 0;
    std::uint64_t elapsed_ms = 0;
};

struct SolveResult {
    Rational count{0};
    bool satisfiable = false;
    SolverStats stats;
};

[[nodiscard]] Instance parse_dimacs_wmc(
    std::istream& input, const ParseLimits& limits = {});
[[nodiscard]] Rational parse_weight(const std::string& text);
[[nodiscard]] SolveResult exact_wmc(
    const Instance& instance, const SolverOptions& options = {});

}  // namespace xai::wmc
