# Results and Evidence

This directory holds the project's phase reports, closeouts, and retained machine-readable or raw validation artifacts. File locations are kept stable because reproduction instructions and reports refer to them directly.

## Phase reports

- [Phase 0 — Scope and research protocol](Phase-0.md)
- [Phase 1 — Contracts and reproducible foundation](Phase-1.md)
- [Phase 2 — Factual ingestion and provenance](Phase-2.md)
- [Phase 3 — Predictive learning](Phase-3.md)
- [Phase 4 — Bounded symbolic and causal reasoning](Phase-4.md)
- [Phase 5 — Bounded adaptation and change monitoring](Phase-5.md)
- [Phase 6 — Calibration and uncertainty diagnostics](Phase-6.md)
- [Phase 7 — Decision policy, certificates, and abstention](Phase-7.md)
- [Phase 8 — Supported input mapping and orchestration](Phase-8.md)
- [Phase 9 — Diagnostic report](Phase-9.md)
- [Phase 9 — Scope-limited closeout](Phase-9-closeout.md)
- [Phase 10 — Reproduction and release audit](Phase-10.md)
- [Phase 10 — Scope-limited closeout](Phase-10-closeout.md)

## Reading the evidence

Phase 9 was closed for project sequencing at a limited 20/98 public-even diagnostic. The frozen benchmark is incomplete and its gate remains **not passed**; no final-odd formula was solver-scored. Phase 10's same-environment build, test, and artifact checks passed, but no independent researcher reproduced the historical benchmark, so the formal reproduction/research-release gate also remains **not passed**. The closeouts state the boundaries explicitly.

## Artifact families

- `Phase-9-*.json`, `Phase-9-*.jsonl`, and `Phase-9-*.log` retain audit, preflight, diagnostic, repeat, and stress-run evidence.
- `Phase-10-*.json` and `Phase-10-*.log` retain the reproduction record, stress rerun record, and validation transcript.
- `Phase-1-manifest.json` and `wmc-public-audit.json` retain the foundation manifest and public archive audit.

For the frozen protocol and test commands, see [`../TESTS/README.md`](../TESTS/README.md) and [`../TESTS/PHASE9_PROTOCOL.md`](../TESTS/PHASE9_PROTOCOL.md). For the full repository map, see [`../Project.md`](../Project.md).
