# XAI Project

XAI is a research project exploring a candidate non-neural architecture that composes explicit evidence tracking, probabilistic learning, bounded adaptation, symbolic and causal reasoning, calibration, and decision-making with abstention.

## Initial bounded task

Phase 0 selected exact weighted model counting on finite, unprojected CNF instances from the 2024 Model Counting Competition Track 2 archive. The development subset contains the 98 public records that pass the exact-rational weight contract; the two public records with non-normalized exact decimal pairs are excluded before scoring. The task has machine-readable input, exact rational outcomes, a documented split, and an independently checkable small-instance oracle. The target is this named eligible subset—not all SAT/WMC workloads or real-world reasoning.

## Current scope

The repository includes a single-threaded C++20 exact WMC reference solver, a public-only archive auditor, a lightweight C++20 shared-record contract library, a bounded exact factual-ingestion component, a versioned sequential predictive harness, a finite-domain reasoning harness, a bounded development-only WMC node-cap adaptation harness with change monitoring and baseline rollback, calibration/uncertainty diagnostics, a decision-policy harness with exact-rational risk comparison and a synthetic certificate gate, and a versioned in-process orchestration harness. The Phase 8 orchestration checks use synthetic fixtures; they do not constitute a production runtime or proof verifier. Phase 9's reduced public-even diagnostic covers 19/98 eligible cases: Ganak solved 10, XAI full and all three ablations solved none, and final-odd cases were not solver-scored. The Phase 9 gate is not passed. Phase 10 clean-build and artifact-reproduction results, the public-claim audit, limitations, unresolved issues, and gate decision are in [RESULT/Phase-10.md](RESULT/Phase-10.md); the exact benchmark protocol and frozen source manifest remain in [TESTS/PHASE9_PROTOCOL.md](TESTS/PHASE9_PROTOCOL.md) and [TESTS/PHASE9_FREEZE.json](TESTS/PHASE9_FREEZE.json).

The interaction layer includes a C++20 plain-text adapter that preserves all well-formed UTF-8 Unicode content in a versioned, provenance-bearing record. Its CLI also decodes UTF-8/16/32 text to canonical UTF-8. It is a pass-through boundary, not a natural-language understanding or answer-generation component; see [TEXT_INPUT_ENGINE.md](TEXT_INPUT_ENGINE.md).

The broader proposal uses established mathematics, including Bayesian updating, sequential Bayesian model averaging, bounded optimization, sequential change detection, weighted model counting, proper scoring rules, conformal prediction, and Bayesian decision theory. The proposed composition remains a hypothesis requiring separate implementation and evaluation. Raw observations still need an extraction and entity-resolution layer; the repository does not solve unrestricted natural-language understanding.

## Repository map

- `README.md` — build, run, and evidence boundary.
- `RESULT/` — Phase 0–10 reports, the incomplete Phase 9 diagnostic, and machine-readable audit/reproduction artifacts.
- `include/`, `src/` — shared contracts, the text-input adapter and CLI, factual ingestion, predictive learning, bounded adaptation, reasoning, calibration, decision policy, and exact WMC parser, solver, and CLI.
- `tools/` — reproducible archive auditor and version-manifest generator.
- `SPEC/` — formal architecture paper, shared contracts, end-to-end contract, and component specifications.
- `TESTS/` — C++20 component and orchestration tests, Phase 9 protocol/evaluator, Phase 10 clean-build/artifact-verification harness, build instructions, and validation results.

## Research discipline

Use task-specific claims, explicit assumptions, baselines, ablations, and held-out evaluation. Report null or negative results. Do not describe the project as first, unprecedented, or independently invented without a scoped prior-art investigation. Passing tests do not prove intelligence or real-data performance.
