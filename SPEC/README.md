# Specification Index

This directory defines the candidate architecture and its mathematical interfaces: models, assumptions, information flow, and failure behavior.

## Documents

- [Shared record contracts](CONTRACTS.md) — versioned envelope, typed IDs, status semantics, canonical serialization, replay/migration policy, and logging boundary.
- [Formal research paper](RESEARCH_PAPER.md) — research framing, mathematical proposal, prior-art limits, and implementation boundary.
- [End-to-end specification](END_TO_END_FLOW.md) — system state, module order, interface contract, and cross-pillar invariants.
- [Training plan](TRAINING_PLAN.md) — component-specific fitting, evidence updates, development-only adaptation, calibration, and holdout rules.

## Component specifications

1. [Factual ingestion](components/01-factual-ingestion.md) — Phase 2 exact finite belief state, evidence/dependence policy, provenance and resource limits.
2. [Learning](components/02-learning.md) — Phase 3 fixed-library sequential WMC completion predictor, log-space mixture updates, partition rules, audit, and replay boundary.
3. [Evolution and adaptation](components/03-evolution-adaptation.md) — Phase 5 bounded WMC node-cap search, development-only objective, Gaussian CUSUM monitor, and fail-closed baseline rollback.
4. [Reasoning](components/04-reasoning.md)
5. [Calibration](components/05-calibration.md) — Phase 6 synthetic-only probability metrics, reliability, sharpness, risk/coverage, and assumption-gated split conformal.
6. [Output and abstention](components/06-output.md) — Phase 7 exact-rational risk, certificate-gated eligibility, structured output, and fail-closed abstention; synthetic verifier only.

## Interpretation

These documents specify a candidate architecture. They do not demonstrate that its models are suitable for a particular task, that its composition is novel, or that it constitutes general intelligence. Assumptions and guarantees apply only within the conditions stated by each component.
