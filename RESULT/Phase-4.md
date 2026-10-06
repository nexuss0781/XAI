# Phase 4 — Bounded symbolic and causal reasoning

**Phase 4 gate: PASS (2026-10-07).** The repository now has an exact finite-state reasoning harness with typed domains, hard constraints, evidence, queries, resource budgets, explicit statuses, and a causal fail-closed policy. Its exact fixtures match an independently written exhaustive oracle; exhausted budgets and unsupported tasks cannot be mistaken for successful answers.

## What was implemented

`xai_reasoning` is a C++20 library in `src/reasoning.cpp`, with its public types in `include/xai/reasoning.hpp`. A task contains variables with finite, distinct value labels; optional non-negative GMP rational weights for each value; hard table constraints expressed as allowed tuples; evidence assignments; and a query assignment. Variable IDs use the shared Phase 1 identifier grammar. Omitted weights mean one, and supplied weights need not be normalized.

For each complete finite assignment satisfying the hard constraints and evidence, the harness multiplies its exact unary weights. It returns the exact evidence mass, query-and-evidence mass, structural satisfiability, and—when the evidence mass is positive—the exact conditional probability. A valid but impossible query returns zero. Structural inconsistency and a zero weighted normalizer remain distinct: the former has no satisfying assignment, while the latter has a satisfying assignment but total mass zero.

Malformed tasks return `invalid_schema`; unsupported constraint grounding returns `unsupported_input`; no satisfying evidence assignment returns `inconsistent`; zero evidence mass returns `zero_normalizer`; an expired deadline returns `timeout`; and exhausted state/table/work bounds return `resource_limit`. Timeout and resource-limit paths emit no partial masses or probability. The causal policy returns `non_identified` when the declared SCM/graph/mechanisms/identification requirements are missing or unsatisfied. Even when all are declared, it returns `unsupported_input` because this phase has no causal estimator or identification verifier; it never emits an intervention result. Bounded synthesis and optimized solver adapters were deferred.

Default bounds are 64 variables, 10,000 constraints, 1,000,000 domain values, 256 bytes per symbol, 1,000,000 table rows, 1,000,000 table cells, 1,000,000 complete assignments, 10,000,000 estimated operations, and 10 seconds. The search enumerates \(S=\prod_i |D_i|\) states. Its worst-case enumeration work is \(O(S[n+e+q+\sum_j k_j\log(r_j+1)])\) before accounting for arbitrary-precision rational bit costs, where \(n\) is variable count, \(e/q\) are evidence/query sizes, and constraint \(j\) has scope width \(k_j\) and \(r_j\) allowed rows. Table compilation is bounded by configured row/cell caps. The documented operation estimate is a structural proxy, not a CPU-time or memory theorem.

## Verification

A fresh GCC 13.3.0 / CMake 3.28.3 / GMP 6.3.0 Release build completed with strict warnings treated as errors. CTest passed **6/6** targets. The new reasoning suite passed seven groups, including exact conditional probabilities and a complementary-event normalization check, satisfiable/unsatisfiable and zero-normalizer separation, malformed and unsupported inputs, resource and timeout behavior, and causal-policy outcomes. An independent recursive Cartesian-product oracle agreed on all **256 possible Boolean relations over three variables**, with deterministic weights, evidence, and query cases.

A fresh Debug build with AddressSanitizer and UndefinedBehaviorSanitizer also passed **6/6** CTest targets without sanitizer findings. `git diff --check` passed. Reproduction commands and the test transcript are recorded in [`TESTS/validation.log`](../TESTS/validation.log); test coverage and limits are summarized in [`TESTS/README.md`](../TESTS/README.md).

## Evidence boundary

These are finite exact software checks, not empirical reasoning or benchmark results. No MCC archive instance or held-out formula was scored, no Ganak comparison was run, and no performance claim is made. The harness supports only explicitly supplied finite domains, unary rational weights, and allowed-tuple hard constraints; it is not a general CNF solver adapter, a natural-language mapper, or a causal inference engine. No causal effect, synthesis result, production behavior, or broader architectural capability is claimed.
