# XAI Project

XAI is a research project exploring a candidate non-neural architecture that composes explicit evidence tracking, probabilistic learning, bounded adaptation, symbolic and causal reasoning, calibration, and decision-making with abstention.

## Initial bounded task

Phase 0 selected exact weighted model counting on finite, unprojected CNF instances from the 2024 Model Counting Competition Track 2 archive. The development subset contains the 98 public records that pass the exact-rational weight contract; the two public records with non-normalized exact decimal pairs are excluded before scoring. The task has machine-readable input, exact rational outcomes, a documented split, and an independently checkable small-instance oracle. The target is this named eligible subset—not all SAT/WMC workloads or real-world reasoning.

## Current scope

The repository includes a single-threaded C++20 exact WMC reference solver, a public-only archive auditor, a lightweight C++20 shared-record contract library, a bounded exact factual-ingestion component, a versioned sequential predictive harness, a finite-domain reasoning harness, a bounded development-only WMC node-cap adaptation harness with change monitoring and baseline rollback, and a calibration/uncertainty diagnostics harness. Phase 1's typed IDs, partition/status rules, canonical JSON format, and replay conventions are documented in [SPEC/CONTRACTS.md](SPEC/CONTRACTS.md). Phase 2 implements finite exact priors, evidence updates, dependency-aware ingestion, provenance, canonical state replay, and a parsed weighted-CNF adapter; see the [factual-ingestion specification](SPEC/components/01-factual-ingestion.md) and [Phase 2 report](RESULT/Phase-2.md). Phase 3 adds a fixed three-expert Bernoulli mixture for predicting exact WMC completion from structural counts; it has fixture-only validation and no benchmark training or performance result. Phase 4 adds exact finite-domain reasoning fixtures. Phase 5 tunes only a bounded WMC node cap on caller-labeled development fixtures and checks CUSUM-triggered rollback. Phase 6 adds synthetic-only binary forecast diagnostics and an assumption-gated conformal boundary; the MCC archive has no labels or calibration split, so no benchmark calibration or coverage claim is made. See the [learning specification](SPEC/components/02-learning.md), [adaptation specification](SPEC/components/03-evolution-adaptation.md), [calibration specification](SPEC/components/05-calibration.md), and [Phase 5](RESULT/Phase-5.md)/[Phase 6](RESULT/Phase-6.md) reports. These are in-process foundations, not persistent storage, authenticated provenance, or a complete runtime. The Phase 0 protocol, data findings, claims, and remaining boundaries are in [RESULT/Phase-0.md](RESULT/Phase-0.md).

The broader proposal uses established mathematics, including Bayesian updating, sequential Bayesian model averaging, bounded optimization, sequential change detection, weighted model counting, proper scoring rules, conformal prediction, and Bayesian decision theory. The proposed composition remains a hypothesis requiring separate implementation and evaluation. Raw observations still need an extraction and entity-resolution layer; the repository does not solve unrestricted natural-language understanding.

## Repository map

- `README.md` — build, run, and evidence boundary.
- `RESULT/` — Phase 0–6 reports and machine-readable public-data audit.
- `include/`, `src/` — shared contracts, factual ingestion, predictive learning, bounded adaptation, reasoning, calibration diagnostics, and exact WMC parser, solver, and CLI.
- `tools/` — reproducible archive auditor and version-manifest generator.
- `SPEC/` — formal architecture paper, shared contracts, end-to-end contract, and component specifications.
- `TESTS/` — C++20 component, contract, WMC, adaptation, and calibration checks, evaluation protocol, build instructions, and validation results.

## Research discipline

Use task-specific claims, explicit assumptions, baselines, ablations, and held-out evaluation. Report null or negative results. Do not describe the project as first, unprecedented, or independently invented without a scoped prior-art investigation. Passing tests do not prove intelligence or real-data performance.
