# Test Notes

## Build and run

Requirements: CMake 3.20+, a C++20 compiler, and GMP/GMPXX development headers and libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake applies `-Wall -Wextra -Werror` (or `/W4 /WX` on MSVC); WMC targets additionally use `-O3` on GCC/Clang and `/O2` on MSVC. The tests build from a fresh directory with no generated files required from an earlier run. Clean-build evidence for Phases 1–8 is in the linked `RESULT/Phase-*.md` reports; earlier transcripts are in [`validation.log`](validation.log), and the Phase 10 run is recorded in [`RESULT/Phase-10.md`](../RESULT/Phase-10.md). Solver input, statuses, compiler options, and resource limits are described in [`WMC_SOLVER.md`](WMC_SOLVER.md).

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

The Phase 0 auditor verifies the archive checksum and that member indices 0–199 each occur exactly once, parses only even-indexed public Track 2 members, and records odd-indexed member names without reading their bodies. The measured Phase 0 summary is in [`RESULT/Phase-0.md`](../RESULT/Phase-0.md), with per-instance audit fields in [`RESULT/wmc-public-audit.json`](../RESULT/wmc-public-audit.json). The later frozen Phase 9 leakage audit did decompress odd-indexed formulas for the predeclared cross-split checks; no final-odd formula was solver-scored or used for tuning.

## Coverage

The shared-contract suite has four focused groups: typed identifiers and partition invariants; malformed records and explicit failure behavior; canonical JSON and schema-mismatch checks; and the complete machine-readable status catalog, including round trips that prove failures are not silently converted to success. The parser rejects duplicate keys, unknown/missing fields, noncanonical JSON, invalid identifiers, and floating-point JSON numbers.

The original six pillar groups check selected finite arithmetic and contract fixtures for evidence updates, a Bayesian model mixture, bounded recombination/change detection, WMC/causal smoke values, proper scores/conformal indexing, and certificate-gated abstention.

The separate WMC suite has eight groups: exact rational parsing; exact WMC and SAT/UNSAT/zero-WMC status; free variables, tautologies, and disconnected components; equivalence across all three solver ablations; strict input and typed-instance validation; node/depth/time limits; fixed exhaustive-oracle formulas; and a 67-case weighted formula grid checked against independent brute-force enumeration. The oracle fixtures are small software checks, not benchmark results.

The Phase 2 factual-ingestion suite has eight groups: exact prior/update normalizers; soft likelihood and zero-normalizer behavior; contradiction, open-world unknowns, and unresolved identity; explicit independent/model-factor/duplicate/correlated/unknown dependence; schema, partition, state, operation, factor-storage, and rational-bit limits; canonical snapshot replay and provenance lineage; parsed weighted-CNF to belief-state integration including repeated clauses; and an independent exhaustive oracle over 64 models and 256 marginals. Phase 2 was also built from a fresh Debug directory with AddressSanitizer and UndefinedBehaviorSanitizer; the result is recorded in [`validation.log`](validation.log).

The Phase 3 predictive-learning suite has six groups: known sequential mixture probabilities and log scores; the relative fixed-expert regret inequality; training/streaming authorization and denied-partition audit; missing features, non-results, duplicate observations, and training-after-stream rejection; deterministic replay/resume plus schema/hash/runtime/tampered-counter checks; and example/context/audit resource limits. Successful predictive records are explicitly approximate because log scores use floating-point arithmetic. The fresh Release and sanitizer builds each passed all five CTest targets; no benchmark outcomes were used.

The Phase 4 reasoning suite has seven groups: exact rational conditional mass/probability with complementary-event normalization; structural SAT/UNSAT versus zero normalizer; query/evidence edge cases; a differential oracle over all 256 Boolean relations on three variables; malformed schema and unsupported grounding; assignment/operation/table/deadline limits with no partial result; and distinct non-identification/unsupported-estimator causal outcomes. Default limits are 64 variables, 10,000 constraints, 1,000,000 domain values, 256 bytes per symbol, 1,000,000 table rows and cells, 1,000,000 assignments, 10,000,000 estimated operations, and 10 seconds. See [`../SPEC/components/04-reasoning.md`](../SPEC/components/04-reasoning.md) and [`../RESULT/Phase-4.md`](../RESULT/Phase-4.md) for semantics and the worst-case complexity statement. Fresh Release and ASan/UBSan CTest each passed 6/6 targets.

The Phase 5 adaptation suite has five groups: bounded WMC node-cap search with exact development-oracle checks and per-candidate score audit; held-out/mixed-partition and duplicate-ID rejection; deterministic replay; development-regression, oracle-failure, and streaming-failure rollback; and CUSUM cadence/alarm, baseline restoration, hold-off, reset, and recovery. The only mutable value is `SolverOptions::node_limit` in `{1,2,4,8,16,32,64,128}`; the named baseline is 128. Ranking maximizes exact development completions and breaks ties toward the smaller cap. The change detector's residual is the log recursive-node count centered by the baseline development mean, under an explicitly configured one-sided Gaussian CUSUM. Its false-alarm and delay behavior is assumption-dependent, and the fixtures do not establish empirical detector quality. See [`../SPEC/components/03-evolution-adaptation.md`](../SPEC/components/03-evolution-adaptation.md) and [`../RESULT/Phase-5.md`](../RESULT/Phase-5.md). Fresh Release and ASan/UBSan CTest each passed 7/7 targets.

The Phase 6 calibration suite has nine focused groups: Brier/log-score definitions and infinite endpoint loss; reliability-bin edge conventions and Wilson intervals; risk/coverage tie groups and missing-outcome accounting; split-conformal order statistics and the `k > n` full-label-set boundary; unsupported-assumption and final-test partition rejection; exact manifest membership and disjoint IDs; subgroup, shift, and empty-outcome diagnostics; probability/resource-limit validation; and the frozen synthetic report's metadata and separate diagnostic outputs. The component does not fit or recalibrate a model. Partition labels and shift/subgroup metadata are caller-supplied, and the hand-authored fixture is not exchangeability-validated. No weighted conformal or posterior-sampling diagnostic is implemented. The Phase 0 WMC archive has no labels or calibration split, so these checks are not benchmark evidence. See [`../SPEC/components/05-calibration.md`](../SPEC/components/05-calibration.md) and [`../RESULT/Phase-6.md`](../RESULT/Phase-6.md). Fresh Release and ASan/UBSan CTest each passed 8/8 targets.

The Phase 7 decision suite has eight focused groups: exact-rational expected-risk ordering and certificate-before-ranking; action and abstention tie rules; invalid probability mass, unavailable loss, duplicate certificate IDs, and the 256-state cap; absent/malformed/stale/mismatched certificates; verifier rejection and exception; no-certified-action, over-budget, and unresolved-state abstention; upstream-status and exactness propagation; and canonical output plus normalized CPU/RAM resource cost. The verifier is synthetic and validates only test fixtures; no production WMC proof-certificate verifier or benchmark-derived loss distribution is implemented. See [`../SPEC/components/06-output.md`](../SPEC/components/06-output.md) and [`../RESULT/Phase-7.md`](../RESULT/Phase-7.md). Fresh Release and ASan/UBSan CTest each passed 9/9 targets.

The Phase 8 orchestration suite has ten groups covering supported-input mapping, provenance and resource manifests, stage traces, explicit skip/failure behavior, partition rejection, synthetic evidence and identity cases, timeout and rollback, certificate-gated abstention, and deterministic semantic replay. These are synthetic in-process fixtures, not a production end-to-end runtime or production certificate verifier. See [`../SPEC/components/07-orchestration.md`](../SPEC/components/07-orchestration.md) and [`../RESULT/Phase-8.md`](../RESULT/Phase-8.md).

The original Phase 9 result is a reduced diagnostic, not a completed benchmark evaluation: it covers 19/98 eligible public-even records, with Ganak exact on 10/19 and XAI full plus each ablation at 0/19. A user-directed continuation added index 38, where all five systems timed out, bringing the preserved stopped artifact to 20/98 with still zero exact XAI counts and no mismatch. No final-odd formula was solver-scored; the exhaustive oracle was eligible for none of the 20 records. The original three over-600-second attempts remain disclosed. A separate same-environment repeat on index 8 reproduces that one row only. The user closed Phase 9 for project sequencing rather than spend an estimated 65 hours on the remaining benchmark; the frozen benchmark gate remains not passed and the closeout makes no full-evaluation claim. See [`../RESULT/Phase-9-closeout.md`](../RESULT/Phase-9-closeout.md), [`../RESULT/Phase-9.md`](../RESULT/Phase-9.md), the [frozen protocol](PHASE9_PROTOCOL.md), the [original 19-row JSONL](../RESULT/Phase-9-public-even.jsonl), the [stopped 20-row JSONL and summary](../RESULT/Phase-9-public-even-stopped.jsonl), and the [index-8 repeat artifacts](../RESULT/Phase-9-public-even-repeat-index8.jsonl).

The repeat wrapper is run from a detached checkout at the frozen code revision, with the current freeze manifest and audit copied in as execution inputs:

```sh
git worktree add --detach /tmp/xai-phase9-frozen a1a65b5ffdeab261bcdeaf7151c03b0c66213403
cp TESTS/PHASE9_FREEZE.json /tmp/xai-phase9-frozen/TESTS/PHASE9_FREEZE.json
cp RESULT/Phase-9-cross-split-audit.json /tmp/xai-phase9-frozen/RESULT/Phase-9-cross-split-audit.json
cp tools/phase9_repeat_case.py /tmp/xai-phase9-frozen/tools/phase9_repeat_case.py
python3 /tmp/xai-phase9-frozen/tools/phase9_repeat_case.py \
  --index 8 \
  --archive "$PWD/data/raw/mc2024-track2-wmc_competition.tar" \
  --freeze /tmp/xai-phase9-frozen/TESTS/PHASE9_FREEZE.json \
  --xai /workspace/XAI-build-phase9-final/xai_wmc_solver \
  --ganak /workspace/XAI/data/vendor/ganak-build/ganak \
  --audit /tmp/xai-phase9-frozen/RESULT/Phase-9-cross-split-audit.json \
  --output "$PWD/RESULT/Phase-9-public-even-repeat-index8.jsonl"
```

The command refuses to overwrite the existing repeat output. The exact binary paths above describe the recorded environment; substitute the manifest's binary paths when reproducing elsewhere.

## Phase 10 reproduction

Run the Phase 10 harness from a new, empty build directory:

```sh
python3 TESTS/phase10_reproduce.py \
  --build-dir "/tmp/xai-phase10-clean-$$" \
  --jobs 2 \
  --report RESULT/Phase-10-reproduction.json \
  --log RESULT/Phase-10-validation.log
```

The harness configures and builds Release, runs all CTest targets and the Phase 9 Python tests, checks committed artifact SHA-256 values and row-level output hashes, verifies the original Phase 9 locked source files against their frozen Git commit, and regenerates both the original partial summary and the one-record repeat summary byte-for-byte. It checks the repeat against original index 8 for formula hash, exact Ganak count, and system statuses, and verifies the frozen archive and binaries. It also checks the compact record of the repeated 11-case synthetic stress suite in [`../RESULT/Phase-10-stress-rerun.json`](../RESULT/Phase-10-stress-rerun.json). These same-environment checks do not satisfy the frozen Phase 9 benchmark gate or Phase 10's independent-reproduction requirement; the Phase 9 project stage was separately closed for sequencing at the user's direction. The JSONL preserves process output and measurements, but does not preserve each decompressed formula body.

`phase10_reproduce.py` refuses a non-empty specified build directory; choose a fresh path rather than pointing it at an existing build. The harness does not invoke the Phase 9 `run` command or score new records; it verifies the separately executed one-record artifact. Phase 10's full independent-reproduction gate remains blocked until a researcher independent of this implementation performs and records the specified reproduction.

## Limits

A passing test establishes only that tested code matched expected answers on the declared fixtures. The contract, ingestion, learning, reasoning, adaptation, calibration, decision, and orchestration components are in-process code; their audit and replay features do not provide persistent storage, authenticated provenance, or cross-process services. Adaptation and calibration checks trust caller-supplied partition labels. The decision policy trusts caller-supplied beliefs, losses, resource measurements, and verifier; its test verifier does not certify real WMC counts. Predictive replay requires a compatible numeric runtime. The Phase 4 enumerator supports explicit finite domains, unary rational weights, and allowed-tuple constraints only; its operation estimate does not bound GMP bit complexity or resident memory. No result establishes correctness on arbitrary formulas, benchmark-scale performance, a speed advantage over Ganak, representative workload coverage, predictive accuracy, real-data calibration or conformal coverage, causal identification, empirical detector guarantees, production proof verification, or capability/safety of the broader architecture. Archive solver evidence is the incomplete 20/98 Phase 9 diagnostic plus a one-record same-environment repeat; no complete Track 2 evaluation, independent reproduction, corpus training, deployment, or natural-language/causal evaluation is claimed.
