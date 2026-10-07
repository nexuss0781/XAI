# XAI

XAI is a research proposal for a candidate modular, non-neural architecture built from explicit mathematical components. The initial bounded empirical task is exact weighted model counting (WMC) on a defined subset of the 2024 Model Counting Competition Track 2 benchmark. This narrow task is not an end-to-end evaluation of the broader architecture.

The repository includes an optimized single-threaded C++20 exact WMC reference solver for unprojected DIMACS-like inputs, a shared C++20 record-contract library, a bounded exact factual-ingestion component with a weighted-CNF adapter, a versioned sequential predictive harness for exact-completion outcomes, a bounded finite-domain symbolic reasoning harness, a development-only WMC node-cap adaptation harness with change monitoring and baseline rollback, a calibration/uncertainty diagnostics harness, an exact-rational decision/abstention harness with a fail-closed certificate interface, and a versioned in-process orchestration harness. The ingestion path provides exact finite marginals, explicit source-dependence semantics, append-only evidence, canonical snapshot replay, provenance, and resource/failure statuses. The learning component uses a fixed three-expert mixture and fixture-only validation; the reasoning component exactly enumerates declared finite tasks and fails closed on unsupported groundings and causal requests. Phases 5–8 use synthetic fixtures for their respective software checks; Phase 8 is not a production runtime. Phase 9 is closed for project sequencing at a scope-limited 20/98 public-even diagnostic: XAI full and its ablations produced no exact completions, and no final-odd formula was solver-scored. The remaining roughly 65-hour frozen benchmark run was deferred; the frozen benchmark gate remains not passed. Phase 10 is closed at the user's direction based on the recorded build, tests, and artifact checks, but no independent researcher reproduced the historical benchmark and the formal release gate remains not passed. See the [Phase 10 closeout](RESULT/Phase-10-closeout.md). There is no production WMC proof verifier or benchmark-derived decision utility. These components do **not** claim general intelligence, novelty, production readiness, or superiority over established solvers.

## Project documents

### Project guides (repository root)

- [Project overview and repository map](Project.md)
- [Component-wise training protocol](TRAINING.md)
- [Training scope and component freeze rules](SPEC/TRAINING_PLAN.md)
- [End-to-end roadmap](ROADMAP.md), [detailed phase plan](Phase.md), and [implementation checklist](TODO.md)

### Specifications (`SPEC/`)

- [Specification index](SPEC/README.md)
- [Shared record contracts](SPEC/CONTRACTS.md)
- [Formal research paper](SPEC/RESEARCH_PAPER.md) and [end-to-end specification](SPEC/END_TO_END_FLOW.md)
- [Predictive learning](SPEC/components/02-learning.md), [bounded reasoning](SPEC/components/04-reasoning.md), and [bounded adaptation](SPEC/components/03-evolution-adaptation.md)
- [Calibration and uncertainty](SPEC/components/05-calibration.md), [decision and abstention](SPEC/components/06-output.md), and [orchestration](SPEC/components/07-orchestration.md)

### Tests and evaluation (`TESTS/`)

- [Test notes and recorded validation](TESTS/README.md)
- [Evaluation plan](TESTS/EVALUATION_PLAN.md), [WMC solver guide](TESTS/WMC_SOLVER.md), and [Phase 9 frozen protocol](TESTS/PHASE9_PROTOCOL.md)

### Results and evidence (`RESULT/`)

- [Results index](RESULT/README.md)
- [Phase 9 scope-limited closeout — project sequencing complete; benchmark incomplete](RESULT/Phase-9-closeout.md)
- [Phase 10 scope-limited closeout — independent-reproduction/release gate not passed](RESULT/Phase-10-closeout.md)

## Build and run

Requirements: CMake 3.20+, a C++20 compiler, and GMP/GMPXX development headers and libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/xai_wmc_solver --timeout-ms 600000 --node-limit 10000000 instance.cnf
```

The solver prints SAT status independently from the exact weighted count: a satisfiable formula can have WMC zero when its satisfying assignments carry zero weight. Unsupported or malformed input and exhausted limits return `s UNKNOWN` with a machine-readable reason; no partial count is emitted. See [TESTS/WMC_SOLVER.md](TESTS/WMC_SOLVER.md) for supported syntax and ablations.

To record hashes for the tracked source and explicitly supplied inputs, see the [version-manifest instructions](TESTS/README.md#version-manifest).

## Evidence boundary

The Phase 0 fixtures establish exact agreement with exhaustive enumeration on the stated small formulas and validation cases. Phases 1–8 provide component and in-process orchestration checks; Phases 5–8 use synthetic fixtures for their respective adaptation, calibration, decision, and orchestration checks. Phase 9 preserves the original 19/98 public-even diagnostic plus one resumed index-38 record (20/98 total): zero exact XAI completions, no observed mismatch, and no final-odd solver scoring. The run was stopped because the remaining frozen evaluation was estimated at about 65 hours. Phase 9 is closed at limited scope; its benchmark remains incomplete and its frozen gate not passed. The Phase 10 clean build, tests, and artifact checks passed, but no independent researcher reproduced the historical solver attempts. Phase 10 is closed at scope-limited evidence at the user's direction; its formal independent-reproduction/research-release gate remains not passed. See the [Phase 9 closeout](RESULT/Phase-9-closeout.md) and [Phase 10 closeout](RESULT/Phase-10-closeout.md). The Phase 0 archive has no outcome labels or calibration split, so Phase 6 metrics are not benchmark calibration evidence. Phase 7's state probabilities are caller-supplied, its exact-result action uses a declared 0/1 loss, and its verifier is synthetic; it does not certify real WMC counts. Partition labels are caller-supplied, not authenticated provenance. No result establishes benchmark-scale performance, general WMC competence, predictive accuracy, a speed advantage over Ganak, representative workload coverage, empirical detector quality, conformal coverage on benchmark or deployment data, causal identification, production proof verification, or capability of the complete architecture. The odd-indexed formula bodies were decompressed only for the predeclared cross-split leakage audit; no final-odd formula was solver-scored or used for tuning.
