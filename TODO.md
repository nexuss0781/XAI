# XAI Phased TODO

This checklist tracks implementation and research work against [ROADMAP.md](ROADMAP.md) and [Phase.md](Phase.md). An unchecked item is not a claim that work has started. A phase is complete only when its exit gate is supported by recorded evidence; source documents or passing toy fixtures alone do not establish empirical capability.

## Current baseline

- [x] Candidate six-pillar architecture and end-to-end information flow are specified in `SPEC/`.
- [x] Deterministic C++20 component fixtures exist for the six pillars.
- [x] GCC strict-warning build and CMake/CTest paths are documented.
- [x] Fresh Release build: CTest 2/2, exact-WMC suite 8/8, 67 weighted exhaustive-oracle cases, and six-pillar suite 6/6; finite software/math checks only. See `TESTS/validation.log`.
- [x] Phase 0 exact-WMC reference solver, strict public-only archive auditor, frozen protocol, and result report are recorded.
- [x] Phase 1 shared contracts, canonical JSON rules, failure-state tests, clean-build guidance, version-manifest tool, and report are recorded; fresh Release CTest 3/3 passed. See `RESULT/Phase-1.md`.
- [x] Phase 5 bounded WMC node-cap adaptation, development-partition checks, CUSUM monitor, baseline rollback, and audit tests are recorded; fresh Release and ASan/UBSan CTest 7/7 passed. See `RESULT/Phase-5.md`.
- [x] Phase 6 versioned synthetic-only calibration diagnostics, partition enforcement, proper scores, reliability/sharpness, risk-coverage, and assumption-gated conformal behavior are recorded; fresh Release and ASan/UBSan CTest 8/8 passed. See `RESULT/Phase-6.md`.
- [x] Phase 7 exact-rational decision policy, fail-closed certificate interface, canonical output, and explicit abstention harness are recorded; fresh Release and ASan/UBSan CTest 9/9 passed. Synthetic verifier only; no production WMC proof verifier. See `RESULT/Phase-7.md`.
- [ ] Implemented end-to-end runtime: not present yet.
- [ ] Ganak comparison, corpus benchmark, and empirical performance results: not run; reserved for Phase 9. No speed or generalization claim is made.
- [ ] Production deployment: out of scope for this research plan.

## Phase 0 — Scope and research protocol

- [x] Select one bounded initial task and write a falsifiable research question: exact rational WMC on the named MCC 2024 Track 2 archive.
- [x] Define target population, permitted use, supported input/output, outcome timing, and task success measure; target is the 98 eligible public/even members.
- [x] Select/inspect candidate data; document coverage, duplicates/near-duplicates, source dependence, missingness, label availability, and selection bias.
- [x] Define split and eligibility: public evens form development candidates; 98 pass the exact-weight contract, two are excluded before scoring; odds remain locked for final evaluation under the same rule. No training/calibration split applies.
- [x] Define leakage controls for exact hashes, source tags, and near-duplicates; document unknown source dependence and the later locked cross-split audit.
- [x] Select Ganak v2.7.0 exact-rational baseline and three targeted ablations before final-test access; baseline is pinned but not yet run.
- [x] Define stress cases, project resource budgets, practical threshold, and stopping criteria.
- [x] Write the claim boundary and identify privacy/safety/domain review needs; no person-level data or human-subject review is involved.
- [x] Record task/data/protocol decisions, exclusions, and alternatives in the Phase 0 report and decision log.
- [x] Implement and verify the bounded C++20 exact-WMC reference solver and streaming public-data auditor.

**Phase 0 gate — PASS (2026-10-07):** The selected 98-instance scope, data-use boundary, eligibility and holdout rules, baseline, metrics, project resource limits, ablations, claims, and clean software verification are documented in [`RESULT/Phase-0.md`](RESULT/Phase-0.md). Two out-of-format public candidates are explicitly excluded; there are no unresolved build or test failures. Corpus performance evaluation remains Phase 9 work.

## Phase 1 — Contracts and reproducible foundation

- [x] Define versioned record envelopes and identifiers for runs, observations, sources, evidence, facts/entities, models, schemas, code builds, partitions, queries, certificates, and results.
- [x] Define machine-readable status/error semantics for success, unsupported input, invalid schema, unknown, inconsistency/zero normalizer, timeout/resource limit, approximation, non-identification, alarm/frozen, certificate rejection, and abstention.
- [x] Specify canonical serialization, numeric rules, replay metadata, schema evolution, and logging/retention boundaries.
- [x] Add shared C++ types and interface/contract tests.
- [x] Add tests for malformed records, schema ID/version mismatch, provenance identifiers, partition labels, serialization round trips, explicit unknown, and distinct failure states.
- [x] Establish clean build/test instructions and a code/data/config version-manifest tool.
- [x] Document the exact limits of deterministic replay and numeric tolerances.

**Phase 1 gate — PASS (2026-10-07):** The interface and status catalog are in [`SPEC/CONTRACTS.md`](SPEC/CONTRACTS.md), shared types/tests are built by CMake, and the clean GCC Release build passes CTest 3/3. Canonical replay preserves success and explicit failure statuses; the full validation record is in [`RESULT/Phase-1.md`](RESULT/Phase-1.md) and [`TESTS/validation.log`](TESTS/validation.log). Persistent storage, authentication, cross-process transport, and later-phase partition enforcement remain out of scope.

## Phase 2 — Factual ingestion and provenance

- [x] Define typed fact/entity schema and unresolved-identity representation.
- [x] Define open-world default and any task-specific closed-world rules.
- [x] Implement immutable evidence records with source/time/original input reference, extractor version, likelihood or constraint, dependency links, and data partition.
- [x] Implement finite prior initialization, evidence update, query marginal, and normalization checks with exact GMP rationals.
- [x] Implement provenance lookup from each query/result to evidence and model/schema/build versions.
- [x] Define and test duplicate/correlated-source behavior; do not default to independent evidence.
- [x] Set finite state and computation limits; return explicit resource/unsupported statuses without approximation fallback.
- [x] Test contradiction/zero normalizer, absent facts as unknown, unresolved identity, malformed likelihoods, replay, and resource limits.

**Phase 2 gate: PASS (2026-10-07).** See [RESULT/Phase-2.md](RESULT/Phase-2.md) and the reproducible transcript in [TESTS/validation.log](TESTS/validation.log). The implementation's identity candidates are annotations, and exact inference remains finite and bounded; archive scoring, split selection, identity resolution, persistence/authentication, and corpus/benchmark evaluation are outside this gate.

## Phase 3 — Predictive learning

- [x] Select and version an initial task-appropriate predictive model library and prior.
- [x] Define feature schema, missing-data behavior, sufficient statistics, and update policy.
- [x] Implement sequential mixture prediction and weight updates in stable log space.
- [x] Track predictive log scores, model weights, model/library hashes, and training-use decisions.
- [x] Enforce training/development/calibration/final-test boundaries in code.
- [x] Define unsupported-input and all-models-zero-probability behavior without silent fallback certainty.
- [x] Test known finite mixture/regret fixtures, replay, state serialization/resume, and partition enforcement.

**Phase 3 gate — PASS (2026-10-07):** The gate passed on the fixed three-expert library with a read-only prediction path, training/streaming-only updates, auditable partition decisions, deterministic same-runtime replay, and a tested relative log-loss bound. Fresh Release CTest 5/5 and fresh ASan/UBSan CTest 5/5 passed. See [RESULT/Phase-3.md](RESULT/Phase-3.md) and [TESTS/validation.log](TESTS/validation.log). No corpus outcomes were read or evaluated; empirical predictive performance remains future work.

## Phase 4 — Bounded symbolic and causal reasoning

- [x] Define typed finite task representation, domains, allowed-tuple hard constraints, unary weights, evidence, query, and solver budgets.
- [x] Implement small exact finite SAT/WMC reference path and explicit status/result contract.
- [x] Add an independent recursive oracle and property checks for satisfiable, unsatisfiable, normalized, zero-denominator, malformed, and over-budget cases; all 256 three-variable Boolean relations agree.
- [x] Defer solver-adapter selection until benchmarking demonstrates a need; no optimized adapter is included in this phase.
- [x] Specify causal model requirements and return `non_identified` when required declarations/identification support are absent; return explicit unsupported status when an estimator is not implemented.
- [x] Ensure timeout and unsupported grounding have distinct explicit statuses and no partial results; synthesis is deferred and has no result path in this phase.
- [x] Defer optional bounded program synthesis; it is not required by the initial task.

**Phase 4 gate — PASS (2026-10-07):** Fresh Release CTest 6/6 and ASan/UBSan CTest 6/6 passed; the independent oracle agreed on all 256 three-variable Boolean relations. Exactness, failure statuses, caps, causal policy, and the claim boundary are documented in [RESULT/Phase-4.md](RESULT/Phase-4.md), [SPEC/components/04-reasoning.md](SPEC/components/04-reasoning.md), and [TESTS/validation.log](TESTS/validation.log). No corpus or performance result was produced.

## Phase 5 — Bounded adaptation and change monitoring

- [x] Name mutable parameter `xai_wmc.SolverOptions::node_limit`, allowed set `{1,2,4,8,16,32,64,128}`, development-completion objective, candidate scan, version format, 128-node baseline, and rollback rules.
- [x] Enforce development-only candidate scoring; reject final-test-labeled suites/records before formula payloads are scored. Partition labels remain caller-supplied and are not authenticated.
- [x] Implement deterministic bounded grid search; full covariance adaptation is unnecessary for this one-parameter finite fixture.
- [x] Define the log-node residual, shared-variance Gaussian in-control/shift distributions, CUSUM threshold, sample cadence, and hold-off/reset policy; state that false-alarm/delay behavior is assumption-dependent.
- [x] Implement shift-alarm, invariant-failure, and objective-regression freeze with versioned baseline rollback and auditable candidate scores/state transitions.
- [x] Test bounds, deterministic replay, regression detection, alarm/freeze, rollback, hold-off, reset, recovery, and final-test rejection.
- [x] Document that bounded parameters do not establish system-wide stability and that detector guarantees depend on residual assumptions.

**Phase 5 gate — PASS (2026-10-07):** No candidate is generated outside the declared finite node-cap grid; labeled final-test inputs are rejected before payload evaluation; alarms, invariant failures, and objective regression restore the baseline and freeze search. Fresh Release and ASan/UBSan CTest each passed 7/7. See [RESULT/Phase-5.md](RESULT/Phase-5.md), [SPEC/components/03-evolution-adaptation.md](SPEC/components/03-evolution-adaptation.md), and [TESTS/validation.log](TESTS/validation.log). Validation used synthetic fixtures only; caller-supplied partition labels are not cryptographic provenance, and no empirical detector guarantee is claimed.

## Phase 6 — Calibration and diagnostics

- [x] Freeze and version the synthetic calibration fixture and partition manifest separately from fitting and reserved final-test IDs; no MCC archive split is claimed.
- [x] Implement Brier and unclipped log scores with score definitions, sample counts, standard errors, and explicit infinite endpoint loss.
- [x] Add equal-width reliability summaries with counts, mean predictions, event rates, and Wilson intervals.
- [x] Add split-conformal binary classification with explicit exchangeability gating, finite-sample order statistic, and tested `k > n` infinite-threshold/full-label-set behavior.
- [x] Weighted conformal was not needed or evaluated: no covariate-shift sample or defensible density-ratio assumptions are available.
- [x] Add tie-aggregated risk-versus-coverage, sample-size, missing-outcome, subgroup-count, sharpness, and caller-flagged shift diagnostics.
- [x] Simulation-based calibration/posterior predictive checks are not applicable to the current task; no posterior-sampling inference algorithm is implemented.
- [x] Keep proper scores, reliability, sharpness, risk/coverage, conformal status, and shift indicators as separate outputs.

**Phase 6 gate — PASS for the synthetic diagnostic harness (2026-10-07):** Fresh Release and ASan/UBSan CTest each passed 8/8; the calibration executable passed 9/9 focused groups. The versioned fixture is hand-authored and synthetic only; the Phase 0 WMC archive has no labels/calibration split, no benchmark calibration or coverage claim is made, and no odd-indexed holdout body was opened. See [RESULT/Phase-6.md](RESULT/Phase-6.md), [SPEC/components/05-calibration.md](SPEC/components/05-calibration.md), [TESTS/validation.log](TESTS/validation.log), and the [published implementation commit](https://github.com/nexuss0781/XAI/commit/aca3725).

## Phase 7 — Decision, certificates, and abstention

- [x] Define the exact-result action, state-dependent loss contract, normalized CPU/RAM cost, abstention cost, risk aggregation, and deterministic tie-breaking.
- [x] Define certificate fields, property `xai.wmc.exact-rational-result.v1`, verifier identity/version, input/result SHA-256 bindings, and validity interval.
- [x] Implement fail-closed eligibility: absent, malformed, stale, mismatched, or verifier-rejected certificates exclude an action.
- [x] Validate exact probability mass, complete non-negative losses, resource costs, and structured output; unresolved mass and non-success upstream states lead to explicit abstention.
- [x] Emit canonical structured decision/abstention output with exact expected risk/cost, lineage, assumptions, certificate status, exactness, and limitations.
- [x] Test risk ordering, action and abstention tie behavior, malformed values, verifier rejection/exception, certificate mismatch, resource limits, and no-certified-action behavior.

**Phase 7 gate — PASS (2026-10-07), for the in-process harness.** Fresh Release and ASan/UBSan CTest each passed 9/9; the dedicated suite passed 8/8 groups. The verifier is synthetic only and does not establish a production exact-count proof. See [RESULT/Phase-7.md](RESULT/Phase-7.md) and [TESTS/validation.log](TESTS/validation.log).

**Published implementation commit:** [cff2409](https://github.com/nexuss0781/XAI/commit/cff2409).

## Phase 8 — Supported input mapping and orchestration

- [x] Select supported input path: caller-provided structured, unprojected DIMACS-WMC with a fixed deterministic parser; caller partition labels are not authenticated.
- [x] Define input-to-formal-task mapping and preserve original input/reference, extractor version, uncertainty, identity candidates, and rejected/unsupported spans.
- [x] Implement versioned orchestration that calls only needed modules and records which were skipped and why.
- [x] Propagate component statuses without coercion; enforce schema, run-state, and partition boundaries end to end, rejecting final-test before formula hashing/parsing.
- [x] Emit a canonical run manifest with code/schema/formula/supplemental-input/model/config hashes, seeds, effective limits, versions, timing, and resource measurements.
- [x] Add end-to-end fixtures for success, unknown/inconsistent/correlated evidence, ambiguous identity, unsupported content, solver timeout, shift alarm/rollback, skipped calibration, invalid/missing certificate, and abstention.
- [x] Verify deterministic semantic replay; encode adaptation metrics losslessly and exclude variable timing/resource measurements.

**Phase 8 gate — PASS (2026-10-07), for the synthetic in-process harness.** Fresh Release and ASan/UBSan CTest runs passed 10/10 targets; focused orchestration groups passed 10/10 in both. The new tests use synthetic inputs and verifier only. No archive formula or outcome was run, the public eligible subset was not evaluated, and no odd-indexed holdout body was opened. See [RESULT/Phase-8.md](RESULT/Phase-8.md), [SPEC/components/07-orchestration.md](SPEC/components/07-orchestration.md), [TESTS/cpp/orchestration_tests.cpp](TESTS/cpp/orchestration_tests.cpp), and [TESTS/validation.log](TESTS/validation.log).

**Implementation commit:** [4e2c709](https://github.com/nexuss0781/XAI/commit/4e2c709).

## Phase 9 — Task-specific evaluation

- [ ] Freeze the protocol, data snapshot, preprocessing, splits, metric definitions, baselines, ablations, thresholds, search budget, seeds, and stopping rules.
- [ ] Run baselines, full pipeline, and ablations with comparable data and tuning budgets.
- [ ] Run stress tests for duplicates/correlation, contradictions, identity errors, missing evidence, drift, out-of-domain input, solver/resource limits, and corrupted records.
- [ ] Report task utility and uncertainty, proper scores, calibration, conformal coverage/set size only under assumptions, risk/coverage, provenance/extraction errors, failure/abstention counts, and resource use.
- [ ] Record all exclusions, failures, confidence intervals, seeds, code/data/config versions, and protocol deviations.
- [ ] Keep final test data untouched; invalidate a compromised run and obtain a new test set before further claims.
- [ ] Report null or negative results and revise/narrow/stop the hypothesis as warranted.

**Phase 9 gate:** Results are reconstructable, answer the predeclared task question, and support only the bounded claims warranted by the protocol.

## Phase 10 — Reproduction and research release

- [ ] Rebuild and rerun from a clean environment/build directory using documented commands.
- [ ] Verify reported tables/plots/summaries regenerate from committed scripts and match run manifests/hashes.
- [ ] Audit README, specifications, roadmap, test notes, and reports for claims beyond the evidence.
- [ ] Publish protocol, limitations, failures, data access constraints, and null/negative findings without exposing restricted data or secrets.
- [ ] Record unresolved issues and a continue/narrow/stop decision.
- [ ] Treat any production or high-impact deployment as a separate proposal and review.

**Phase 10 gate:** Independent reproduction meets stated tolerance and all public claims match the evidence.

## Blockers and decisions

Record blockers here with owner/context, date, and the phase gate they affect. Do not silently move a blocked item to complete.

- No open Phase 0 blockers. Two out-of-format public candidates are excluded under the exact-rational eligibility rule; Phase 9 corpus/baseline evaluation remains pending and does not block the Phase 0 scope-and-protocol gate.
- No open Phase 1 blockers. Shared contracts are an in-process foundation only; storage, authentication, runtime integration, and Phase 3 partition enforcement remain later-phase work.
- No open Phase 2 gate blockers. Phase 2 provides canonical in-process snapshots and checks model/evidence partition consistency, but not persistent storage, authentication, identity resolution, archive eligibility, or split selection.
- Phase 7 and Phase 8 harness gates are complete; a production verifier for an actual exact-WMC proof and task-specific benchmark evaluation remain unimplemented. The eligible Phase 0 public subset is reserved for the frozen Phase 9 evaluation; the odd-indexed holdout remains locked.
