# XAI Phased TODO

This checklist tracks implementation and research work against [ROADMAP.md](ROADMAP.md) and [Phase.md](Phase.md). An unchecked item is not a claim that work has started. A phase is complete only when its exit gate is supported by committed evidence; source documents or passing toy fixtures alone do not establish empirical capability.

## Current baseline

- [x] Candidate six-pillar architecture and end-to-end information flow are specified in `SPEC/`.
- [x] Deterministic C++20 component fixtures exist for the six pillars.
- [x] GCC strict-warning build and CMake/CTest paths are documented.
- [x] Existing checks were run during roadmap preparation: six component groups passed and CTest passed (2026-10-07). These remain finite software/math checks only.
- [ ] Implemented end-to-end runtime: not present yet.
- [ ] Corpus-based training, task benchmark, and empirical performance results: not present yet.
- [ ] Production deployment: out of scope for this research plan.

## Phase 0 — Scope and research protocol

- [ ] Select one bounded initial task and write a falsifiable research question.
- [ ] Define target population, permitted use, supported input/output, outcome timing, and task success measure.
- [ ] Select/inspect candidate data; document coverage, duplicates/near-duplicates, source dependence, missingness, label quality, and bias.
- [ ] Define training, development, calibration (if used), untouched final-test, and temporal/domain-shift partitions.
- [ ] Define leakage controls for entities, sources, and near-duplicates.
- [ ] Select simple task-appropriate baselines and targeted ablations before final-test access.
- [ ] Define stress cases, resource budgets, minimum practically meaningful result, and stopping criteria.
- [ ] Write the claim boundary and identify any required privacy, safety, or domain review.
- [ ] Record task/data/protocol decisions and alternatives in a decision log.

**Phase 0 gate:** The question, data-use boundaries, split plan, baselines, metrics, and claims are reviewable before fitting or final-test access.

## Phase 1 — Contracts and reproducible foundation

- [ ] Define versioned record envelopes and identifiers for runs, observations, evidence, facts, models, schemas, partitions, queries, certificates, and outputs.
- [ ] Define machine-readable status/error semantics for success, unsupported input, unknown, inconsistency, timeout, approximation, non-identification, alarms, certificate rejection, and abstention.
- [ ] Specify canonical serialization, numeric rules, replay metadata, schema evolution, and logging/retention boundaries.
- [ ] Add shared C++ types and interface/contract tests.
- [ ] Add tests for malformed records, schema mismatch, provenance identifiers, partition labels, serialization round trips, and explicit failure states.
- [ ] Establish clean build/test and version-manifest procedure.
- [ ] Document the exact limits of deterministic replay and numeric tolerances.

**Phase 1 gate:** Components can exchange versioned records and preserve errors; a clean build and tests pass from a fresh build directory.

## Phase 2 — Factual ingestion and provenance

- [ ] Define typed fact/entity schema and unresolved-identity representation.
- [ ] Define open-world default and any task-specific closed-world rules.
- [ ] Implement immutable evidence records with source/time/original input reference, extractor version, likelihood or constraint, dependency links, and data partition.
- [ ] Implement finite prior initialization, evidence update, query marginal, and normalization checks.
- [ ] Implement provenance lookup from each query/result to evidence and model/schema versions.
- [ ] Define and test duplicate/correlated-source behavior; do not default to independent evidence.
- [ ] Set finite state and computation limits; implement explicit approximation/unsupported behavior.
- [ ] Test contradiction/zero normalizer, absent facts as unknown, unresolved identity, malformed likelihoods, replay, and resource limits.

**Phase 2 gate:** Every returned belief is traceable and all defined failure paths are explicit.

## Phase 3 — Predictive learning

- [ ] Select and version an initial task-appropriate predictive model library and prior.
- [ ] Define feature schema, missing-data behavior, sufficient statistics, and update policy.
- [ ] Implement sequential mixture prediction and weight updates in stable log space.
- [ ] Track predictive log scores, model weights, model/library hashes, and training-use decisions.
- [ ] Enforce training/development/calibration/final-test boundaries in code.
- [ ] Define unsupported-input and all-models-zero-probability behavior without silent fallback certainty.
- [ ] Test known finite mixture/regret fixtures, replay, state serialization/resume, and partition enforcement.

**Phase 3 gate:** Predictions/updates are replayable and auditable; guarantees are stated only relative to the declared library and priors.

## Phase 4 — Bounded symbolic and causal reasoning

- [ ] Define typed finite task representation, domains, hard constraints, weights, evidence, query, and solver budgets.
- [ ] Implement small exact SAT/WMC reference path and explicit status/result contract.
- [ ] Add independent oracle/differential and property tests for satisfiable, unsatisfiable, normalized, zero-denominator, malformed, and over-budget cases.
- [ ] Define solver adapter/selection only after benchmarking a justified need.
- [ ] Specify causal model requirements and return `non_identified` when the encoded query is not identified.
- [ ] Ensure solver timeout, unsupported grounding, and incomplete synthesis return `unknown`/explicit status rather than false unsatisfiability.
- [ ] Defer optional bounded program synthesis unless required by the initial task; if required, define grammar, verifier, and timeout semantics.

**Phase 4 gate:** Exact reference fixtures match independent results and solver limits/failures cannot be mistaken for successful reasoning.

## Phase 5 — Bounded adaptation and change monitoring

- [ ] Name mutable parameters, permitted bounds, owning module, objective, baseline, version, and rollback rules.
- [ ] Enforce development-only candidate evaluation and prevent final-test data from reaching adaptation code.
- [ ] Implement initial bounded search/recombination; add full covariance adaptation only if required and fully tested.
- [ ] Define monitored residuals, reference/shift distributions, thresholds, sample cadence, and alarm/hold-off/reset policy.
- [ ] Implement alarm/invariant/regression freeze and baseline rollback with auditable state transitions.
- [ ] Test bounds, regression detection, alarm/freeze, rollback, restart, and replay.
- [ ] Document that bounded parameters do not establish system-wide stability and detector properties are assumption-dependent.

**Phase 5 gate:** No out-of-bound or final-test-driven update is possible; alarm/failure selects the baseline.

## Phase 6 — Calibration and diagnostics

- [ ] Freeze and version a calibration partition distinct from fitting and final testing.
- [ ] Implement proper scores relevant to the task, including score definitions and sample accounting.
- [ ] Add reliability summaries with bin definitions/counts and uncertainty where appropriate.
- [ ] Add split-conformal prediction only where exchangeability and score assumptions are defensible; test the `k > n` unbounded-set case.
- [ ] If needed, implement weighted conformal under explicit covariate-shift/density-ratio assumptions and test unsupported cases.
- [ ] Add risk-versus-coverage, sample-size, missing-outcome, subgroup-count, and shift diagnostics.
- [ ] Add simulation-based calibration/posterior predictive checks where relevant to inference code.
- [ ] Keep score, calibration, sharpness, conformal coverage, and shift outputs separate.

**Phase 6 gate:** Reports include method, assumptions, partition, versions, sample counts, and limitations; no unsupported coverage claim is made.

## Phase 7 — Decision, certificates, and abstention

- [ ] Define task-specific actions, loss, resource cost, abstention cost, and tie-breaking rules.
- [ ] Define certificate policy, format, verifier/version, and exact property established.
- [ ] Implement fail-closed eligibility: absent, invalid, stale, or mismatched required certificate excludes an action.
- [ ] Validate probability mass, loss, cost, and output schema; map unresolved high-risk states to explicit failure/abstention.
- [ ] Emit structured decision/abstention with expected risk, cost, lineage, assumptions, certificate status, and limitations.
- [ ] Test risk ordering, tie behavior, malformed values, verifier failure, certificate mismatch, and no-certified-action cases.

**Phase 7 gate:** No inadmissible action can be selected or described as certified; abstention is a working path.

## Phase 8 — Supported input mapping and orchestration

- [ ] Select supported input path: structured typed input, constrained deterministic parser, or explicitly named permitted preprocessing component.
- [ ] Define input-to-formal-task mapping and preserve original input/reference, extractor version, uncertainty, and rejected/unsupported content.
- [ ] Implement versioned orchestration that calls only needed modules and records which were skipped and why.
- [ ] Propagate component statuses without coercion; enforce schema, state-version, and data-partition boundaries end to end.
- [ ] Emit run manifest with code/schema/data/model/config hashes, seeds, limits, versions, timing, and resource use.
- [ ] Add end-to-end fixtures for success, absent/contradictory evidence, correlated sources, ambiguous identity, unsupported input, solver timeout, shift alarm, calibration limitation, invalid certificate, and abstention.
- [ ] Verify deterministic replay or define stochastic seeds/tolerances.

**Phase 8 gate:** A clean supported input yields a traceable output or explicit failure/abstention, with all transitions and skipped modules auditable.

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

- None recorded. Initial task, population, dataset, and empirical success criterion remain to be selected in Phase 0.
