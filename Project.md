# XAI Project

XAI is a research project exploring a candidate non-neural architecture that composes explicit evidence tracking, probabilistic learning, bounded adaptation, symbolic and causal reasoning, calibration, and decision-making with abstention.

## Initial bounded task

Phase 0 selected exact weighted model counting on finite, unprojected CNF instances from the 2024 Model Counting Competition Track 2 archive. The development subset contains the 98 public records that pass the exact-rational weight contract; the two public records with non-normalized exact decimal pairs are excluded before scoring. The task has machine-readable input, exact rational outcomes, a documented split, and an independently checkable small-instance oracle. The target is this named eligible subset—not all SAT/WMC workloads or real-world reasoning.

## Current scope

The repository includes a single-threaded C++20 exact WMC reference solver, a public-only archive auditor, a lightweight C++20 shared-record contract library, a bounded exact factual-ingestion component, a versioned sequential predictive harness, a finite-domain reasoning harness, a bounded development-only WMC node-cap adaptation harness with change monitoring and baseline rollback, calibration/uncertainty diagnostics, a decision-policy harness with exact-rational risk comparison and a synthetic certificate gate, and a versioned in-process orchestration harness. The Phase 8 orchestration checks use synthetic fixtures; they do not constitute a production runtime or proof verifier. Phase 9 is closed for project sequencing at a scope-limited 20/98 public-even diagnostic: Ganak solved 10, XAI full and all three ablations solved none, and final-odd formulas were not solver-scored. The roughly 65-hour remaining frozen benchmark was deferred; its gate remains not passed. Phase 10 is closed at the user's direction based on the verified build, tests, and recorded-artifact checks, but no independent researcher reproduced the benchmark experiment and the formal research-release gate remains not passed. See the [Phase 9 closeout](RESULT/Phase-9-closeout.md) and [Phase 10 closeout](RESULT/Phase-10-closeout.md); detailed Phase 10 evidence and limits are in [RESULT/Phase-10.md](RESULT/Phase-10.md). The exact benchmark protocol and frozen source manifest remain in [TESTS/PHASE9_PROTOCOL.md](TESTS/PHASE9_PROTOCOL.md) and [TESTS/PHASE9_FREEZE.json](TESTS/PHASE9_FREEZE.json).

The broader proposal uses established mathematics, including Bayesian updating, sequential Bayesian model averaging, bounded optimization, sequential change detection, weighted model counting, proper scoring rules, conformal prediction, and Bayesian decision theory. The proposed composition remains a hypothesis requiring separate implementation and evaluation. Raw observations still need an extraction and entity-resolution layer; the repository does not solve unrestricted natural-language understanding.

## Repository map

The repository is grouped by purpose. Existing paths are kept stable because reports, build definitions, and reproduction instructions link to them.

```text
README.md, Project.md       Entry points, scope, and repository map
TRAINING.md                 Detailed component-wise training protocol
ROADMAP.md, Phase.md, TODO.md
                            Research roadmap, phase gates, and work checklist
include/xai/                Public C++ component interfaces
src/                        C++ implementations and command-line entry point
SPEC/                       Architecture, contracts, component specs, training plan
TESTS/                       C++/Python tests, protocols, and validation notes
RESULT/                      Phase reports, closeouts, and retained evidence artifacts
tools/                        Archive, manifest, and frozen-evaluation utilities
build/                        Local generated CMake output (ignored by Git)
```

For training work, start with [`TRAINING.md`](TRAINING.md) for the detailed procedure and [`SPEC/TRAINING_PLAN.md`](SPEC/TRAINING_PLAN.md) for the architectural scope and data/freeze rules. They are complementary: the former is the working protocol, while the latter records what “training” means for each component and what remains outside the present implementation. See [`SPEC/README.md`](SPEC/README.md) for the specification index, [`TESTS/README.md`](TESTS/README.md) for validation instructions, and [`RESULT/README.md`](RESULT/README.md) for phase evidence.

## Research discipline

Use task-specific claims, explicit assumptions, baselines, ablations, and held-out evaluation. Report null or negative results. Do not describe the project as first, unprecedented, or independently invented without a scoped prior-art investigation. Passing tests do not prove intelligence or real-data performance.
