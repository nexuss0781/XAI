# XAI Phase Plan

This document is the execution-level companion to [ROADMAP.md](ROADMAP.md). A phase is complete only when its exit gate is met, evidence is recorded, and relevant documentation is updated. The phases are research gates, not calendar estimates. Do not mark a phase complete merely because code compiles or a local fixture passes.

## Dependency path

```text
0 Scope and protocol
  └─ 1 Contracts and reproducible foundation
       ├─ 2 Factual ingestion ─┐
       ├─ 3 Predictive learning ├─ 5 Bounded adaptation ─┐
       └─ 4 Bounded reasoning ─┘                         │
            2–5 ── 6 Calibration and diagnostics ────────┤
            2–6 ── 7 Decision and output gate ───────────┤
            1–7 ── 8 Input mapping and orchestration ────┤
            0–8 ── 9 Task-specific evaluation ───────────┤
                      └─ 10 Reproduction and release ───┘
```

Phases 2–4 can be developed in parallel after Phase 1, but their integration must respect shared state and data-use contracts. Adaptation depends on a stable development objective and baseline, calibration needs stable predictions plus a separate calibration partition, and decision/output depends on declared risks and certificate semantics. Orchestration comes after component interfaces are stable enough to preserve their guarantees.

## Phase 0 — Scope and research protocol

**Goal:** Turn the general architecture proposal into one bounded, falsifiable research question.

**Work:**

- Select a task whose input, output, outcome, target population, and intended use can be stated precisely. If no task is selected, stop at specifications and component validation; do not invent a dataset or success claim.
- Define supported input and output types, data permissions, preprocessing, label/outcome timing, and whether an initial caller supplies typed facts or a constrained representation.
- Inspect candidate data before training: record counts, duplicate and near-duplicate rates, missingness, label quality, source dependence, population coverage, machine-readable fraction, and known collection bias.
- Define the primary utility/quality measure, uncertainty reporting, resource budget, task-appropriate baseline(s), ablations, stress cases, and minimum practically meaningful result.
- Freeze partition policy: training, development, calibration if used, untouched final test, and temporal/domain-shift set. Define entity/source/near-duplicate leakage controls.
- Write a claim statement and non-claims. Identify whether a privacy, safety, or domain-specific review is needed before data access or experiments.

**Deliverables:** short task card; data card; protocol and metric definitions; baseline/ablation plan; explicit claim boundary; decision log for unresolved choices.

**Exit gate:** A reviewer can tell what result would support or fail the hypothesis, which records may be used for which purpose, and what cannot be claimed. Final test data and tuning rules are protected before any model selection.

**Do not:** treat a corpus byte size as evidence of coverage; tune on the final test set; equate a passed math fixture with task performance.

## Phase 1 — Shared contracts and reproducible foundation

**Goal:** Give every component the same versioning, provenance, status, and reproducibility conventions.

**Work:**

- Define typed identifiers for run, observation, source, evidence, fact/entity, model, schema, code build, partition, query, certificate, and result.
- Define common envelope fields: schema/version IDs, status, value or structured result, assumptions, lineage, exact/approximate designation, resource budget and consumption, warnings/errors, and reproducibility metadata.
- Define statuses for at least success, unsupported input, invalid schema, unknown, inconsistent/zero normalizer, timeout/resource limit, approximate, non-identified, alarm/frozen, certificate rejected, and abstention. Status must be machine-readable and never silently converted to success.
- Specify canonical serialization, numeric conventions, stable identifiers, deterministic replay boundaries, schema migration policy, and failure logging. Keep secrets and restricted raw data out of ordinary logs.
- Add interface-level C++ types and focused tests without prematurely selecting persistent storage or a large framework. Preserve C++20 and strict compiler warnings unless a documented decision changes them.
- Establish clean-build and CTest instructions, repeatable fixture execution, and a version manifest for code/data/config.
- Extend tests to cover malformed records, explicit unknown, partition labels, and serialization round trips.

**Deliverables:** interface/schema document; status/error catalog; lightweight shared types; build/test instructions; tests for contract invariants; decision log.

**Exit gate:** Every component can exchange a versioned record and represent failure without an ambiguous numeric fallback. A clean build and tests pass from a fresh build directory.

## Phase 2 — Factual ingestion and provenance

**Goal:** Turn supported typed claims into an auditable evidence ledger and a coherent finite belief state.

**Work:**

- Define a typed fact/entity vocabulary and represent unresolved identity explicitly; document open-world behavior and any task-specific closed-world rules.
- Implement immutable evidence records with source, time, original observation/reference, extractor version, likelihood or constraint semantics, dependency links, and data-use partition.
- Implement the simplest declared finite model first: prior initialization, evidence update, query marginal, and normalizer checks. Use a stable numerical representation appropriate to the tested state size.
- Make source-dependence handling explicit. Duplicate evidence is linked and cannot be treated as independent by default. Document the policy for unknown dependence.
- Define finite state-size and computation limits. If exact inference does not fit, return a supported approximation with method and error/bound where available, or an explicit unsupported/limit status.
- Persist or serialize enough state to reproduce a query and reconstruct lineage; defer a full database choice until the task and scale justify it.

**Deliverables:** evidence/fact schemas; finite belief update and query path; provenance lookup; edge-case and property tests; recorded limits and dependency policy.

**Exit gate:** Tests cover normalized updates, contradiction/zero normalizer, missing facts as unknown, duplicate/correlated source handling, unresolved identity, serialization/replay, and resource-limit behavior. Each returned marginal can be traced to evidence and model versions.

**Status (2026-10-07): PASS.** The implemented scope, limits, validation evidence, and non-claims are recorded in [RESULT/Phase-2.md](RESULT/Phase-2.md). Corpus evaluation remains reserved for Phase 9.

## Phase 3 — Predictive learning

**Goal:** Add inspectable sequential predictions over a declared task-specific model library.

**Work:**

- Select a small model library and prior for the Phase 0 task; define feature schema, missing-data behavior, model versioning, and the update policy.
- Implement predictive mixture weights and updates in log space using stable log-sum-exp. Track sufficient statistics and cumulative predictive log score for supported models.
- Enforce the partition contract: update only on authorized training/streaming records; never fit or tune on calibration/final-test data.
- Handle unsupported features, malformed outcomes, missing observations, and all-models-zero-probability events with explicit statuses; do not inject a silent fallback probability.
- Test the sequential mixture inequality on known fixtures and test replay determinism, streaming/chunk equivalence where applicable, and version compatibility.
- Define serialization/resume behavior and model-library migration; preserve prior and library hashes with each result.

**Deliverables:** versioned model library; prediction/update interface; training-use audit; test fixtures; replayable predictive state and scores.

**Exit gate:** Repeated runs from the same state and sequence produce the same result within declared numeric tolerances; data partition policy is enforced; score and model-weight results are auditable. The relative mixture bound is described as relative to the declared library, not as an absolute accuracy promise.

**Status (2026-10-07): PASS.** The versioned completion-outcome predictor, update/audit policy, replayable state, model/prior hashes, and finite-mixture fixture evidence are recorded in [RESULT/Phase-3.md](RESULT/Phase-3.md) and [SPEC/components/02-learning.md](SPEC/components/02-learning.md). No benchmark outcome was used; predictive performance remains outside this gate and is deferred to Phase 9.

## Phase 4 — Bounded symbolic and causal reasoning

**Goal:** Answer only finite, typed questions supported by the chosen solver and supplied model.

**Work:**

- Define the typed finite task representation: variables/domains, hard constraints, weights, evidence, query, and explicit solver/resource limits.
- Implement a minimal exact reference path (small exhaustive enumeration is acceptable initially) for SAT and weighted model counting; select an optimized solver only when measured need and correctness tests justify it.
- Add independent cross-check fixtures and property tests for satisfiable/unsatisfiable formulas, probability normalization, zero denominators, zero weights, malformed constraints, and state-space limits.
- Keep causal inference separate: require a declared structural causal model, graph, mechanisms, and identification conditions before returning an intervention result. Return `non_identified` if not supported; do not infer causation from association.
- Treat bounded synthesis as optional and defer it until the core task needs it. If added, define a finite grammar and verifier; timeout/exhaustion returns `unknown` unless the search was complete over the declared space.

**Deliverables:** typed reasoning schema; finite SAT/WMC implementation; solver adapter only if justified; causal status policy; differential and boundary tests.

**Exit gate:** Exact fixtures agree with an independent oracle; inconsistent input, zero denominator, timeout, unsupported grounding, and non-identification produce distinct explicit outcomes. Worst-case complexity and enforced limits are documented.

**Status (2026-10-07): PASS.** The typed finite-domain harness, exact rational WMC/query path, independent 256-relation oracle grid, distinct failure statuses, explicit budgets, and causal fail-closed policy are recorded in [RESULT/Phase-4.md](RESULT/Phase-4.md). Fresh Release and ASan/UBSan CTest runs each passed 6/6. No corpus performance or causal-identification claim is made.

## Phase 5 — Bounded adaptation and change monitoring

**Goal:** Permit controlled configuration search without allowing unbounded or opaque self-modification.

**Work:**

- Name every mutable parameter, permitted set/bounds, owning module, objective, candidate generator, version format, baseline, and rollback mechanism.
- Restrict objective computation and candidate ranking to development data. Keep final test inaccessible to the adaptation implementation and its operators during tuning.
- Start with a simple bounded search/recombination procedure. Implement a full covariance-adaptation strategy only if the research question warrants it and tests can cover its actual update rules.
- Specify residual definitions, in-control/shift distributions, thresholds, sample cadence, false-alarm trade-offs, reset/hold-off policy, and how adaptation affects residual assumptions.
- On a shift alarm, invariant failure, or unresolved objective regression, freeze updates and select the versioned baseline. Log the trigger, state transition, and rollback evidence.
- Test hard parameter bounds, deterministic replay, regression detection, alarm/freeze, reset/hold-off, and recovery to baseline.

**Deliverables:** bounded adaptation interface; development-only optimizer; detector configuration; baseline/rollback path; alarm and regression tests.

**Exit gate:** No update can exceed declared bounds or use final-test data; alarms and invariant failures freeze exploration and restore the baseline. Document that projection does not prove system-wide stability and detector guarantees are assumption-dependent.

**Status (2026-10-07): PASS.** The finite WMC node-cap search, development-only objective boundary, Gaussian CUSUM policy, versioned baseline rollback, reset/hold-off behavior, and assumption limits are recorded in [SPEC/components/03-evolution-adaptation.md](SPEC/components/03-evolution-adaptation.md) and [RESULT/Phase-5.md](RESULT/Phase-5.md). Fresh Release and ASan/UBSan CTest each passed 7/7; no benchmark outcomes were used.

## Phase 6 — Calibration and uncertainty diagnostics

**Goal:** Measure probabilistic outputs honestly and make coverage/risk assumptions visible.

**Work:**

- Freeze a calibration dataset separate from model fitting and final testing; version the split and selection policy.
- Implement proper scores (including Brier and log score where applicable), reliability summaries with declared bins, counts and uncertainty, and risk-versus-coverage diagnostics.
- Add split conformal prediction only for a task with an explicit nonconformity score and a defensible exchangeability setup. Implement the finite-sample order-statistic boundary, including the unbounded-set case when the required index exceeds calibration sample size.
- If evaluating a weighted conformal variant, state and test the covariate-shift and density-ratio assumptions; do not describe it as protection against arbitrary shift.
- Add diagnostics for sample-size limitations, missing outcomes, subgroup counts, shift indicators, and simulation-based calibration/posterior predictive checks if relevant to the inference code.
- Keep score, calibration, sharpness, coverage, and shift as separate outputs; never collapse them to a universal confidence value.

**Deliverables:** versioned calibration protocol; metrics implementation; calibration reports; assumption and sample-count checks; test cases for unsupported assumptions.

**Exit gate:** Metrics are computed only on authorized partitions and include definitions, sample counts, versions, and assumptions. No conformal coverage guarantee is emitted when its assumptions cannot be defended.

**Status (2026-10-07): PASS for the synthetic diagnostic harness.** The versioned calibration protocol, partition-manifest enforcement, proper scores, reliability and sharpness summaries, risk/coverage, subgroup/missingness/shift diagnostics, and fail-closed split-conformal boundary are implemented and tested. Fresh Release and ASan/UBSan CTest each passed 8/8. The MCC archive has no labels or calibration split, so this is not benchmark calibration evidence; the hand-authored fixture makes no conformal coverage claim, and no odd-indexed holdout body was opened. See [RESULT/Phase-6.md](RESULT/Phase-6.md), [SPEC/components/05-calibration.md](SPEC/components/05-calibration.md), [TESTS/validation.log](TESTS/validation.log), and the [published implementation commit](https://github.com/nexuss0781/XAI/commit/aca3725).

## Phase 7 — Decision policy, certificates, and abstention

**Goal:** Select among bounded candidate outputs/actions using a declared loss model and fail-closed certificate policy.

**Work:**

- Define candidate action schema, state-dependent loss, resource cost, abstention action/cost, risk aggregation, and tie-breaking behavior for the selected task.
- Define which actions require which certificate, the certificate format, verifier identity/version, verifier input coverage, and what property is actually established.
- Implement the action gate so missing, invalid, stale, or mismatched required certificates make the action inadmissible. Keep abstention available as an ordinary decision.
- Validate probability masses and loss values; explicit uncertainty or malformed values must lead to an explicit failure/abstention rather than fabricated confidence.
- Emit an output record with action or abstention, expected risk/cost, evidence and model lineage, assumptions, certificate status, exact/approximate status, and limitations.
- Test risk ordering, tie behavior, invalid probability, unavailable loss, certificate mismatch, verifier failure, and all-actions-rejected cases.

**Deliverables:** task-specific decision policy; certificate interface and test verifier; structured output schema; fail-closed tests.

**Exit gate:** Every chosen action is admissible under the declared policy; every required certificate is checked; an absent/invalid certificate excludes an action; unsupported inputs cannot produce an unqualified answer. Document that certificates prove only the encoded property under verifier assumptions.

## Phase 8 — Supported input mapping and end-to-end orchestration

**Goal:** Connect component contracts into a reproducible full path without claiming unrestricted language understanding.

**Work:**

- Choose a supported input route for the Phase 0 task: structured facts, a constrained deterministic parser, or an explicitly named preprocessing component whose permission, version, and errors are recorded.
- Define task-to-formal-representation validation. Preserve original input/reference and record extraction candidates, uncertainty, unresolved identity, and rejected/unsupported spans where relevant.
- Implement a versioned orchestrator that routes records through required modules, labels skipped components, enforces partition and state-version boundaries, and carries error/status results forward unchanged.
- Define run manifest and replay: configuration, code/schema/data/model hashes, seeds, budgets, module versions, timing/resource use, and data-use decisions.
- Add end-to-end fixtures for ordinary success, missing facts, contradictory/correlated evidence, unsupported input, entity ambiguity, solver timeout, shift alarm, calibration limitation, invalid certificate, and abstention.
- Verify deterministic replay for deterministic modules and define tolerance/random-seed behavior for stochastic modules.

**Deliverables:** supported input contract; orchestration layer; run manifest; end-to-end replay fixtures; documented boundaries for unsupported inputs and skipped modules.

**Exit gate:** A clean run consumes a supported input and returns either a fully traced structured output or an explicit failure/abstention. Every transition is auditable; no component failure is converted to confident success.

## Phase 9 — Frozen task-specific evaluation

**Goal:** Test the research hypothesis on a named task without contaminating the final evaluation.

**Work:**

- Freeze the data snapshot, population, preprocessing, splits, metrics, baseline and ablation configurations, thresholds, random seeds, hyperparameter search budget, and stopping rule.
- Run the simplest task-appropriate baseline(s), the full pipeline, and targeted ablations with comparable input, data access, and tuning budgets.
- Analyze leakage, duplicate/source/entity dependence, coverage, label quality, and uncertainty in the sample. Record any deviations before interpreting results.
- Run the planned stress set: copied/correlated sources, conflicting evidence, identity mistakes, missing evidence, temporal/domain shift, out-of-domain input, solver/resource limits, corrupted data, and unsupported queries.
- Report task metrics and intervals; proper scores and calibration; coverage/set size under assumptions; selective risk/coverage; provenance/extraction errors; failures/abstentions; runtime, memory, storage, I/O, and approximation behavior.
- Preserve raw result summaries and exact versions. Do not repair a disappointing final result by tuning on it. If the protocol is compromised, invalidate it and obtain a new untouched test set.

**Deliverables:** frozen protocol; run manifests; baseline/ablation/stress reports; reproducible result artifacts; claims supported by actual outcomes.

**Exit gate:** Another researcher can reconstruct the experiment and see all exclusions/failures. The results answer the declared question and support only bounded task/data/implementation claims. Null or negative findings count as complete results.

## Phase 10 — Reproduction and research release

**Goal:** Make the evidence, code, and public description consistent and reproducible.

**Work:**

- Re-run from a clean environment/build directory using documented commands and pinned versions where practical.
- Verify that the committed test/evaluation scripts regenerate the reported summaries and that generated artifacts carry matching hashes and metadata.
- Audit all public claims in README, specifications, roadmap, tests, and any paper/report against the evidence; remove unsupported performance, novelty, safety, or intelligence implications.
- Publish the protocol, implementation limits, known defects, data access constraints, and null/negative outcomes alongside the result. Do not publish restricted raw data or secrets.
- Record unresolved items and decide whether to continue, narrow, or stop. Any operational deployment proposal is a separate project with its own review and approval.

**Deliverables:** reproducibility instructions and evidence; claim audit; research release notes; updated limitation and open-issue log.

**Exit gate:** A clean reproduction succeeds within documented tolerance, documentation matches measured evidence, and there is no implication that component fixtures establish general capability or real-world safety.

## Completion record

Record each phase as `not started`, `in progress`, `blocked`, or `complete` in [TODO.md](TODO.md). For each completion, link the implementation, tests, protocol/report, and commit that provide the evidence. A checkbox without a corresponding artifact is not a gate pass.
