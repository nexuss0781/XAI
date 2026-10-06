# XAI Project

XAI is a research project exploring a candidate non-neural architecture that composes explicit evidence tracking, probabilistic learning, bounded adaptation, symbolic and causal reasoning, calibration, and decision-making with abstention.

## Initial bounded task

Phase 0 selected exact weighted model counting on finite, unprojected CNF instances from the 2024 Model Counting Competition Track 2 archive. The development subset contains the 98 public records that pass the exact-rational weight contract; the two public records with non-normalized exact decimal pairs are excluded before scoring. The task has machine-readable input, exact rational outcomes, a documented split, and an independently checkable small-instance oracle. The target is this named eligible subset—not all SAT/WMC workloads or real-world reasoning.

## Current scope

The repository includes a single-threaded C++20 exact WMC reference solver, a public-only archive auditor, a lightweight C++20 shared-record contract library, and a bounded exact factual-ingestion component. Phase 1's typed IDs, partition/status rules, canonical JSON format, and replay conventions are documented in [SPEC/CONTRACTS.md](SPEC/CONTRACTS.md). Phase 2 implements finite exact priors, evidence updates, dependency-aware ingestion, provenance, canonical state replay, and a parsed weighted-CNF adapter; see the [factual-ingestion specification](SPEC/components/01-factual-ingestion.md) and [Phase 2 report](RESULT/Phase-2.md). The contracts and ingestion component are in-process foundations, not persistent storage, authentication, or a complete runtime. The Phase 0 protocol, data findings, claims, and remaining boundaries are in [RESULT/Phase-0.md](RESULT/Phase-0.md).

The broader proposal uses established mathematics, including Bayesian updating, sequential Bayesian model averaging, bounded optimization, sequential change detection, weighted model counting, proper scoring rules, conformal prediction, and Bayesian decision theory. The proposed composition remains a hypothesis requiring separate implementation and evaluation. Raw observations still need an extraction and entity-resolution layer; the repository does not solve unrestricted natural-language understanding.

## Repository map

- `README.md` — build, run, and evidence boundary.
- `RESULT/` — Phase 0–2 reports and machine-readable public-data audit.
- `include/`, `src/` — shared contracts, bounded factual ingestion, and exact WMC parser, solver, and CLI.
- `tools/` — reproducible archive auditor and version-manifest generator.
- `SPEC/` — formal architecture paper, shared contracts, end-to-end contract, and component specifications.
- `TESTS/` — C++20 component, contract, and WMC checks, evaluation protocol, build instructions, and validation results.

## Research discipline

Use task-specific claims, explicit assumptions, baselines, ablations, and held-out evaluation. Report null or negative results. Do not describe the project as first, unprecedented, or independently invented without a scoped prior-art investigation. Passing tests do not prove intelligence or real-data performance.
