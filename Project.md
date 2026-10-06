# XAI Project

XAI is a research project exploring a candidate non-neural architecture that composes explicit evidence tracking, probabilistic learning, bounded adaptation, symbolic and causal reasoning, calibration, and decision-making with abstention.

## Initial bounded task

Phase 0 selected exact weighted model counting on finite, unprojected CNF instances from the 2024 Model Counting Competition Track 2 archive. The development subset contains the 98 public records that pass the exact-rational weight contract; the two public records with non-normalized exact decimal pairs are excluded before scoring. The task has machine-readable input, exact rational outcomes, a documented split, and an independently checkable small-instance oracle. The target is this named eligible subset—not all SAT/WMC workloads or real-world reasoning.

## Current scope

The repository includes a single-threaded C++20 exact WMC reference solver and a public-only archive auditor. The solver uses GMP rational arithmetic, unit propagation, component decomposition, and occurrence-based branching. It is a narrow implementation exercise; it does not validate the composition or performance of the full six-pillar architecture. The Phase 0 protocol, data findings, claims, and remaining boundaries are recorded in [RESULT/Phase-0.md](RESULT/Phase-0.md).

The broader proposal uses established mathematics, including Bayesian updating, sequential Bayesian model averaging, bounded optimization, sequential change detection, weighted model counting, proper scoring rules, conformal prediction, and Bayesian decision theory. The proposed composition remains a hypothesis requiring separate implementation and evaluation. Raw observations still need an extraction and entity-resolution layer; the repository does not solve unrestricted natural-language understanding.

## Repository map

- `README.md` — build, run, and evidence boundary.
- `RESULT/` — Phase 0 report and machine-readable public-data audit.
- `include/`, `src/` — exact WMC parser, solver, and CLI.
- `tools/` — reproducible benchmark archive auditor.
- `SPEC/` — formal architecture paper, end-to-end contract, and component specifications.
- `TESTS/` — C++20 component and WMC checks, evaluation protocol, build instructions, and recorded results.

## Research discipline

Use task-specific claims, explicit assumptions, baselines, ablations, and held-out evaluation. Report null or negative results. Do not describe the project as first, unprecedented, or independently invented without a scoped prior-art investigation. Passing tests do not prove intelligence or real-data performance.
