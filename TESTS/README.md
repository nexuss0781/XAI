# Test Notes

## Build and run

Requirements: CMake 3.20+, a C++20 compiler, and GMP/GMPXX development headers and libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake applies `-Wall -Wextra -Werror` (or `/W4 /WX` on MSVC); WMC targets additionally use `-O3` on GCC/Clang and `/O2` on MSVC. The tests build from a fresh directory with no generated files required from an earlier run. Clean-build evidence for Phases 1–5 is in [`RESULT/Phase-1.md`](../RESULT/Phase-1.md), [`RESULT/Phase-2.md`](../RESULT/Phase-2.md), [`RESULT/Phase-3.md`](../RESULT/Phase-3.md), [`RESULT/Phase-4.md`](../RESULT/Phase-4.md), and [`RESULT/Phase-5.md`](../RESULT/Phase-5.md); the reproducible transcripts are in [`validation.log`](validation.log). Solver input, statuses, compiler options, and resource limits are described in [`WMC_SOLVER.md`](WMC_SOLVER.md).

## Version manifest

From the repository root, run:

```sh
python3 tools/create_version_manifest.py \
  --output build/version-manifest.json \
  --data data/raw/benchmark.tar \
  --config config/evaluation.json
```

Repeat `--data` and `--config` for each file that affects a run; omit either option when that category is not applicable. The script records the current Git revision, whether tracked files differ from that revision, SHA-256 and byte size for every Git-tracked source/document file, plus hashes and sizes for explicitly supplied data/config files. File contents are never copied into the manifest. Ignored or untracked files are not silently included in the tracked-source set; pass all run-relevant data and configuration explicitly. Keep the manifest with the run report or build artifact, not in a directory exposed to unauthorized readers if file names themselves are sensitive.

## Phase 0 archive audit

To reproduce the public-only benchmark audit, obtain the official CC BY 4.0 archive from the [Zenodo record](https://zenodo.org/records/14249068) and run:

```sh
python3 tools/audit_wmc_archive.py \
  data/raw/mc2024-track2-wmc_competition.tar \
  --output RESULT/wmc-public-audit.json
```

The auditor verifies the archive checksum and that member indices 0–199 each occur exactly once, parses only even-indexed public Track 2 members, and records odd-indexed member names without reading their bodies. The measured Phase 0 summary is in [`RESULT/Phase-0.md`](../RESULT/Phase-0.md), with per-instance audit fields in [`RESULT/wmc-public-audit.json`](../RESULT/wmc-public-audit.json).

## Coverage

The shared-contract suite has four focused groups: typed identifiers and partition invariants; malformed records and explicit failure behavior; canonical JSON and schema-mismatch checks; and the complete machine-readable status catalog, including round trips that prove failures are not silently converted to success. The parser rejects duplicate keys, unknown/missing fields, noncanonical JSON, invalid identifiers, and floating-point JSON numbers.

The original six pillar groups check selected finite arithmetic and contract fixtures for evidence updates, a Bayesian model mixture, bounded recombination/change detection, WMC/causal smoke values, proper scores/conformal indexing, and certificate-gated abstention.

The separate WMC suite has eight groups: exact rational parsing; exact WMC and SAT/UNSAT/zero-WMC status; free variables, tautologies, and disconnected components; equivalence across all three solver ablations; strict input and typed-instance validation; node/depth/time limits; fixed exhaustive-oracle formulas; and a 67-case weighted formula grid checked against independent brute-force enumeration. The oracle fixtures are small software checks, not benchmark results.

The Phase 2 factual-ingestion suite has eight groups: exact prior/update normalizers; soft likelihood and zero-normalizer behavior; contradiction, open-world unknowns, and unresolved identity; explicit independent/model-factor/duplicate/correlated/unknown dependence; schema, partition, state, operation, factor-storage, and rational-bit limits; canonical snapshot replay and provenance lineage; parsed weighted-CNF to belief-state integration including repeated clauses; and an independent exhaustive oracle over 64 models and 256 marginals. Phase 2 was also built from a fresh Debug directory with AddressSanitizer and UndefinedBehaviorSanitizer; the result is recorded in [`validation.log`](validation.log).

The Phase 3 predictive-learning suite has six groups: known sequential mixture probabilities and log scores; the relative fixed-expert regret inequality; training/streaming authorization and denied-partition audit; missing features, non-results, duplicate observations, and training-after-stream rejection; deterministic replay/resume plus schema/hash/runtime/tampered-counter checks; and example/context/audit resource limits. Successful predictive records are explicitly approximate because log scores use floating-point arithmetic. The fresh Release and sanitizer builds each passed all five CTest targets; no benchmark outcomes were used.

The Phase 4 reasoning suite has seven groups: exact rational conditional mass/probability with complementary-event normalization; structural SAT/UNSAT versus zero normalizer; query/evidence edge cases; a differential oracle over all 256 Boolean relations on three variables; malformed schema and unsupported grounding; assignment/operation/table/deadline limits with no partial result; and distinct non-identification/unsupported-estimator causal outcomes. Default limits are 64 variables, 10,000 constraints, 1,000,000 domain values, 256 bytes per symbol, 1,000,000 table rows and cells, 1,000,000 assignments, 10,000,000 estimated operations, and 10 seconds. See [`../SPEC/components/04-reasoning.md`](../SPEC/components/04-reasoning.md) and [`../RESULT/Phase-4.md`](../RESULT/Phase-4.md) for semantics and the worst-case complexity statement. Fresh Release and ASan/UBSan CTest each passed 6/6 targets.

The Phase 5 adaptation suite has five groups: bounded WMC node-cap search with exact development-oracle checks and per-candidate score audit; held-out/mixed-partition and duplicate-ID rejection; deterministic replay; development-regression, oracle-failure, and streaming-failure rollback; and CUSUM cadence/alarm, baseline restoration, hold-off, reset, and recovery. The only mutable value is `SolverOptions::node_limit` in `{1,2,4,8,16,32,64,128}`; the named baseline is 128. Ranking maximizes exact development completions and breaks ties toward the smaller cap. The change detector's residual is the log recursive-node count centered by the baseline development mean, under an explicitly configured one-sided Gaussian CUSUM. Its false-alarm and delay behavior is assumption-dependent, and the fixtures do not establish empirical detector quality. See [`../SPEC/components/03-evolution-adaptation.md`](../SPEC/components/03-evolution-adaptation.md) and [`../RESULT/Phase-5.md`](../RESULT/Phase-5.md). Fresh Release and ASan/UBSan CTest each passed 7/7 targets.

## Limits

A passing test establishes only that tested code matched the expected answer on those fixtures. The contract, factual-ingestion, learning, reasoning, and adaptation components are in-process libraries; audit and replay features do not make them persistent storage, authenticated provenance, or cross-process services. Adaptation checks trust caller-supplied partition labels; they reject records labeled `final_test` but cannot detect a caller that mislabels or leaks test data. Predictive replay requires a compatible numeric runtime. The Phase 4 enumerator supports explicit finite domains, unary rational weights, and allowed-tuple constraints only; its operation estimate does not bound GMP bit complexity or resident memory. No test establishes correctness on arbitrary formulas, benchmark-scale accuracy, empirical speed, superiority over Ganak, dataset representativeness, predictive accuracy, causal identification, shift-detector guarantees, or behavior of the broader architecture. No full Track 2 solving experiment, corpus adaptation/training, calibration study, deployment, or natural-language/causal evaluation is claimed.
