# XAI

XAI is a research proposal for a candidate modular, non-neural architecture built from explicit mathematical components. The initial bounded empirical task is exact weighted model counting (WMC) on a defined subset of the 2024 Model Counting Competition Track 2 benchmark. This narrow task is not an end-to-end evaluation of the broader architecture.

The repository includes an optimized single-threaded C++20 exact WMC reference solver for unprojected DIMACS-like inputs, a shared C++20 record-contract library, a bounded exact factual-ingestion component with a weighted-CNF adapter, and a versioned sequential predictive harness for exact-completion outcomes. The ingestion path provides exact finite marginals, explicit source-dependence semantics, append-only evidence, canonical snapshot replay, provenance, and resource/failure statuses. The learning component uses a fixed three-expert mixture and fixture-only validation; no benchmark outcomes were used for training or scored. These components do **not** claim general intelligence, novelty, production readiness, or superiority over established solvers.

## Project documents

- [Project overview](Project.md)
- [Phase 0 protocol and result](RESULT/Phase-0.md)
- [Phase 1 contracts and validation report](RESULT/Phase-1.md)
- [Phase 2 factual-ingestion and provenance report](RESULT/Phase-2.md)
- [Phase 3 predictive-learning report](RESULT/Phase-3.md)
- [Shared record contracts](SPEC/CONTRACTS.md)
- [Predictive-learning specification](SPEC/components/02-learning.md)
- [WMC solver build, input, and limits](TESTS/WMC_SOLVER.md)
- [Specification index](SPEC/README.md)
- [Formal research paper](SPEC/RESEARCH_PAPER.md)
- [End-to-end specification](SPEC/END_TO_END_FLOW.md)
- [Test notes and recorded validation](TESTS/README.md)
- [Evaluation plan](TESTS/EVALUATION_PLAN.md)
- [End-to-end roadmap](ROADMAP.md)
- [Detailed phase plan](Phase.md)
- [Phased implementation checklist](TODO.md)

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

The Phase 0 fixtures establish exact agreement with exhaustive enumeration on the stated small formulas and validation cases. The Phase 1 tests check record invariants and deterministic serialization fixtures. Phase 2 adds exact finite-model and parser-adapter fixtures, and Phase 3 adds fixed-library sequential-prediction and partition-boundary fixtures; these are software checks, not corpus evaluation. None of these phases establishes benchmark-scale performance, general WMC competence, predictive accuracy, a speed advantage over Ganak, representative workload coverage, or capability of the complete six-pillar system. No benchmark training or broad natural-language evaluation is part of this task.
