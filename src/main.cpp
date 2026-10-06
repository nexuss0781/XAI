#include "xai/wmc.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {

[[nodiscard]] std::uint64_t parse_option(const std::string& text, const char* name) {
    std::uint64_t value = 0;
    const auto [position, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || position != text.data() + text.size()) {
        throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

void usage(std::ostream& out) {
    out << "Usage: xai_wmc_solver [--timeout-ms N] [--node-limit N] [--max-depth N]\n"
           "                    [--no-unit-propagation] [--no-components] [--first-branch] FILE|-\n"
           "Exact rational WMC for bounded DIMACS-like unprojected CNF.\n";
}

std::string one_line(std::string message) {
    for (char& ch : message) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    return message;
}

}  // namespace

int main(int argc, char** argv) {
    xai::wmc::SolverOptions solver_options;
    xai::wmc::ParseLimits parse_limits;
    std::string input_path;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--help" || argument == "-h") {
                usage(std::cout);
                return 0;
            }
            if (argument == "--no-unit-propagation") {
                solver_options.unit_propagation = false;
            } else if (argument == "--no-components") {
                solver_options.component_decomposition = false;
            } else if (argument == "--first-branch") {
                solver_options.occurrence_branching = false;
            } else if (argument == "--timeout-ms" || argument == "--node-limit" ||
                       argument == "--max-depth") {
                if (i + 1 >= argc) throw std::invalid_argument(argument + " requires a value");
                const auto value = parse_option(argv[++i], argument.c_str());
                if (argument == "--timeout-ms") {
                    if (value > 1'000'000'000'000ULL) {
                        throw std::invalid_argument("--timeout-ms exceeds safe chrono range");
                    }
                    solver_options.timeout_ms = value;
                }
                if (argument == "--node-limit") solver_options.node_limit = value;
                if (argument == "--max-depth") {
                    if (value > std::numeric_limits<std::size_t>::max()) {
                        throw std::invalid_argument("--max-depth exceeds size_t");
                    }
                    solver_options.max_depth = static_cast<std::size_t>(value);
                }
            } else if (!argument.empty() && argument[0] == '-') {
                if (argument == "-") {
                    if (!input_path.empty()) throw std::invalid_argument("only one input is allowed");
                    input_path = argument;
                } else {
                    throw std::invalid_argument("unknown option: " + argument);
                }
            } else if (input_path.empty()) {
                input_path = argument;
            } else {
                throw std::invalid_argument("only one input is allowed");
            }
        }
        if (input_path.empty()) {
            usage(std::cerr);
            return 2;
        }
        std::ifstream file;
        std::istream* input = &std::cin;
        if (input_path != "-") {
            file.open(input_path, std::ios::binary);
            if (!file) throw xai::wmc::ParseError("cannot open input file: " + input_path);
            input = &file;
        }
        const auto instance = xai::wmc::parse_dimacs_wmc(*input, parse_limits);
        const auto result = xai::wmc::exact_wmc(instance, solver_options);
        std::cout << "s " << (result.satisfiable ? "SATISFIABLE" : "UNSATISFIABLE") << '\n';
        std::cout << "c s exact rational " << result.count.get_str() << '\n';
        std::cout << "c xai status=solved variables=" << instance.variables
                  << " clauses=" << instance.declared_clauses
                  << " nodes=" << result.stats.recursive_nodes
                  << " decisions=" << result.stats.decisions
                  << " propagations=" << result.stats.propagations
                  << " component_splits=" << result.stats.component_splits
                  << " max_depth=" << result.stats.maximum_depth
                  << " elapsed_ms=" << result.stats.elapsed_ms << '\n';
        return 0;
    } catch (const xai::wmc::UnsupportedInput& error) {
        std::cout << "s UNKNOWN\nc xai status=unsupported reason=" << one_line(error.what()) << '\n';
        return 3;
    } catch (const xai::wmc::ParseError& error) {
        std::cout << "s UNKNOWN\nc xai status=parse_error reason=" << one_line(error.what()) << '\n';
        return 2;
    } catch (const xai::wmc::ResourceLimit& error) {
        std::cout << "s UNKNOWN\nc xai status=resource_limit reason=" << one_line(error.what()) << '\n';
        return 4;
    } catch (const std::invalid_argument& error) {
        std::cout << "s UNKNOWN\nc xai status=invalid_arguments reason=" << one_line(error.what()) << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cout << "s UNKNOWN\nc xai status=internal_error reason=" << one_line(error.what()) << '\n';
        return 5;
    }
}
