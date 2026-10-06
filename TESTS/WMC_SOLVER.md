# Bounded exact WMC prototype

The C++20 `xai_wmc_solver` reads the supported DIMACS-like Track 2 WMC subset and emits an exact reduced rational. It uses GMP rational arithmetic, recursive DPLL, unit propagation, independent-component factorization, and maximum-occurrence branching. A separate Boolean result tracks SAT independently from WMC, because a satisfiable formula can have WMC 0 when all satisfying assignments have zero weight.

## Build and run

Requirements: C++20 compiler, CMake 3.20+, and GMP/GMPXX development headers/libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/xai_wmc_solver --timeout-ms 600000 --node-limit 10000000 instance.cnf
```

The solver is compiled with `-O3` (or `/O2` on MSVC). For a machine-specific benchmark binary, add `-march=native` only when recording the CPU model and compiler flags in the run manifest. Runs are single-threaded. The internal deadline starts after input parsing; the benchmark harness must enforce the full-process wall-time cap and the 4-GiB resident-memory limit externally. Record the exact compiler, GMP version, CPU, and binary/input hashes.

Use `-` as the filename to read standard input. `--timeout-ms 0` and `--node-limit 0` disable those respective limits; timeout values above 10^12 ms and recursion depths above 4096 are rejected as unsafe. `--no-unit-propagation`, `--no-components`, and `--first-branch` expose predeclared solver ablations. On any resource limit, the CLI returns `s UNKNOWN` and a machine-readable `resource_limit` status; it never returns a partial count.

Default structural caps are 1,000,000 variables, 10,000,000 declared/parsed clauses, 100,000,000 literal occurrences, a 16-MiB line, and a 4-GiB input stream. Weight tokens are limited to 128 characters with decimal exponent/scale bounded by 2,000; solver defaults are 10,000,000 recursive nodes, 600,000 ms of search time, and depth 4,096. These input-size caps do not guarantee a 4-GiB memory ceiling. The external runner must classify an OS/cgroup kill or allocation failure as out-of-budget/resource-limit, not as an answer.

## Supported subset and validation

Accepted: `p cnf n m`, zero-terminated clauses (including clauses continued across lines), comments, optional `c t wmc`, and optional `c p weight L W 0` directives. Weights are exact integer fractions or exact finite decimal/scientific values. For each explicitly weighted variable, both signs must be present and lie in [0,1]; their exact sum must be 1. The special pair `(1,1)` is the format’s unweighted/#SAT default and is accepted whether explicitly written or omitted. Any other pair that does not sum exactly to 1 is rejected. Duplicate weight directives, conflicting task markers, negative/out-of-range weights, projected directives, malformed tokens, range errors, missing terminators, and clause-count mismatches fail closed.

Parsing normalizes repeated literals, discards tautological clauses, and preserves empty clauses. These transformations preserve the unprojected CNF's satisfying assignments and WMC. A zero count is not automatically labeled UNSAT: SAT is tracked independently.

## Verification boundary

`TESTS/cpp/wmc_tests.cpp` uses exhaustive exact enumeration as an independent small-instance oracle and exercises malformed input, zero weights, free variables, disconnected components, the three planned ablations, and explicit resource-limit statuses. These are finite code checks only. They do not establish benchmark accuracy at scale, speedup over Ganak, dataset representativeness, or general-purpose reasoning capability.
