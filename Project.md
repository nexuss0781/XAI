# XAI Project

XAI is a research project exploring a candidate non-neural architecture that composes explicit evidence tracking, probabilistic learning, bounded adaptation, symbolic and causal reasoning, calibration, and decision-making with abstention.

## Research objective

Specify a modular, inspectable system whose assumptions and evidence can be traced through a defined computational flow. The present work is a mathematical specification and a set of deterministic component checks. It is not a claim of general intelligence, architectural novelty, production readiness, or empirical superiority.

## Current scope

The proposal is intentionally built from established mathematics, including Bayesian updating, sequential Bayesian model averaging, bounded optimization, sequential change detection, weighted model counting, proper scoring rules, conformal prediction, and Bayesian decision theory. The proposed composition is a hypothesis that requires implementation and controlled evaluation.

Raw observations still need an extraction and entity-resolution layer that maps them into the typed representations used by the specifications. The current repository does not solve unrestricted natural-language understanding. Dataset sufficiency is task-dependent; a 1 GB corpus is not, by itself, evidence of adequate coverage or capability.

## Repository map

- `README.md` — project overview and local build/run instructions.
- `SPEC/` — formal architecture paper, end-to-end contract, and component specifications. Test procedures and test results are excluded from this directory.
- `TESTS/` — C++20 component checks, build instructions, and recorded test output.

## Research discipline

Use task-specific claims, explicit assumptions, baselines, ablations, and held-out evaluation. Report null or negative results. Do not describe the project as first, unprecedented, or independently invented without a scoped prior-art investigation. Do not claim that passing unit tests proves intelligence or real-data performance.