# Phase 2 — Factual ingestion and provenance

**Phase 2 gate: PASS (2026-10-07).** The repository now has a bounded exact factual-ingestion library, a parsed weighted-CNF adapter, immutable evidence-ledger behavior, canonical state replay, query-level provenance, and explicit failure/resource-limit outcomes. The full acceptance record is in [`TESTS/validation.log`](../TESTS/validation.log); the design and limits are specified in [`SPEC/components/01-factual-ingestion.md`](../SPEC/components/01-factual-ingestion.md).

## What was implemented

[`include/xai/ingestion.hpp`](../include/xai/ingestion.hpp) and [`src/ingestion.cpp`](../src/ingestion.cpp) define typed Boolean facts and entity references, exact nonnegative prior potentials, likelihood or hard-constraint evidence, dependency links, data partitions, and a finite belief model. An accepted evidence record is copied into an append-only ledger; callers can inspect only const ledger/model accessors, and there is no record mutation or deletion operation. Each evidence record retains a source ID, UTC observation time, original-observation reference, extractor version, factor scope and values, dependence classification, and partition label.

The model uses GMP exact rationals throughout. It reports the prior normalizer \(Z_0\), the raw evidence normalizer \(Z_E\), the conditional normalizer \(Z_E/Z_0\), and exact unary marginals. Prior potentials need not sum to one, so the encoded model also supports the WMC convention where an unweighted variable has literal potentials `(1,1)`; the returned conditional marginal is not confused with the raw WMC.

The `ingest_weighted_cnf` adapter converts parsed unprojected CNF variables into typed Boolean facts and literal-weight potentials, then maps clauses to deterministic zero/one factors. Each clause is recorded against the source formula; repeated canonical clauses link to their earlier evidence record and are counted once. Clause-factor multiplication represents CNF conjunction, not independent corroboration. Missing facts use open-world behavior, and unresolved subject identity remains attached as annotation; the marginal is explicitly about the fact ID, not a resolved real-world entity.

For dependence, independent likelihood factors require an explicit `independent` declaration. `model_factor` is restricted to structural hard constraints. Exact duplicates require a prior link and identical scope/factor, remain visible in lineage, and are not multiplied twice. Correlated and unknown dependence are recorded but produce `unsupported_input` until a joint likelihood is supplied. Evidence from a different partition cannot enter a model.

Canonical `xai.factual_model` snapshots include facts, unresolved identity candidates, priors, evidence, dependency links, partition, and limits. Restoring a snapshot validates the shared envelope and task schema; replaying the same query produces byte-identical canonical output. Successful `xai.belief_query` records identify the fact, model, schema, code build, run, result and partition, and trace to the evidence IDs and source IDs. Failure records contain explicit machine-readable statuses and diagnostics but no numeric result.

Default exact-model caps are 63 represented facts, 1,048,576 worlds, 10,000 evidence records, 1,000,000 total factor-table entries, 50,000,000 counted inference operations, and 512 bits for each rational numerator/denominator and intermediate value. Rational handling has a hard 8,192-bit ceiling, while the shared canonical-record layer limits records to 16 MiB, 64 nesting levels and one million JSON values. With the default world cap, exhaustive inference is limited to at most 20 facts. These bounds are enforced at evidence insertion, snapshot restoration and query time; exceeding them returns an explicit limit status, never a partial marginal. No approximate fallback is implemented.

## Validation evidence

A fresh GCC 13.3.0, CMake 3.28.3, GMP 6.3.0 Release build compiled under C++20 with strict warnings (`-Wall -Wextra -Werror`; existing WMC targets retain `-O3`). CTest passed **4/4** targets: six-pillar fixtures, shared contracts, the new factual-ingestion suite, and exact WMC. The ingestion executable passed **8/8** groups, covering exact priors and updates, normalized and zero-normalizer behavior, contradictions, open-world unknowns, unresolved identity, duplicate/correlated/unknown source behavior, schema/partition validation, operation/state/factor-table/rational limits, canonical snapshot replay and lineage, and a parser-to-belief weighted-CNF integration case.

The property-style fixture generated **64** exact four-fact models and compared all **256** queried marginals and normalizers against independent direct enumeration over 16 possible worlds. The parser-backed example used two weighted variables, two distinct clauses, and one repeated clause; it returned exact WMC `1/3`, posterior marginals `P(x1=true)=2/5` and `P(x2=true)=1`, and preserved the duplicate link. Additional tests verified that unweighted `(1,1)` potentials produce raw WMC `1` and normalized evidence mass `1/2` for a unit clause, that soft likelihoods yield the exact `3/7` posterior, and that contradictory hard evidence returns `inconsistent` rather than a number.

A separate fresh AddressSanitizer/UndefinedBehaviorSanitizer Debug build passed CTest **4/4** with leak detection and UBSan halt-on-error enabled. No sanitizer finding was reported. `git diff --check` and Python syntax checks also passed. Reproduce the builds from the repository root with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Gate boundary

The exactness guarantee is for the finite model as encoded; it does not validate priors, likelihoods, extractors, source reliability, identity correspondence, or real-world truth. Entity candidates are annotation-only—there is no identity-resolution or entity-equality inference. The model stores a canonical replayable snapshot, not a database, authentication layer or access-control service. The Phase 2 component checks that evidence matches its model partition, but it does not audit archive eligibility or choose data splits.

No MCC 2024 archive member was ingested or scored for this phase, and no odd-indexed holdout body was opened. There is no Ganak comparison, benchmark completion rate, runtime result, corpus-scale claim, approximation method, or production deployment. The Phase 0 eligible 98-record development scope and locked holdout rules remain unchanged; corpus evaluation remains future Phase 9 work.
