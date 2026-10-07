# Phase 9 scope-limited closeout

**Project Phase 9 status: CLOSED FOR PROJECT SEQUENCING, SCOPE-LIMITED (2026-10-07).**
**Frozen WMC benchmark gate: NOT PASSED.** Closing the project stage is not a claim that the predeclared benchmark was completed or passed.

At the user's direction, the remaining frozen benchmark work was stopped because completing it was estimated to require about **65 hours**. That long run is not a requirement for moving the project to Phase 10. The project proceeds with a bounded, honestly reported diagnostic; no one should describe it as a complete Track 2 evaluation.

## Evidence retained

The original frozen public-even diagnostic contains **19/98** eligible records. A resumed run added only public-even index **38**, then stopped at the user's request. The combined stopped artifact therefore contains **20/98** public-even records. The additional index 38 attempt timed out for Ganak and all four XAI configurations. Across the 20 records, there were no observed count mismatches, but XAI full and every ablation still had **zero exact completions and zero directly verified counts**. The independent exhaustive oracle was eligible for **0/20** records. No final-odd formula was solver-scored; the existing leakage audit remains a separate pre-scoring check.

| System | Exact completions | Other outcomes in 20 records |
|---|---:|---|
| Ganak | 10/20 | 6 memory limits; 4 timeouts |
| XAI full | 0/20 | 11 resource limits; 7 timeouts; 2 memory limits |
| XAI without unit propagation | 0/20 | 12 resource limits; 5 timeouts; 3 memory limits |
| XAI without components | 0/20 | 13 resource limits; 5 timeouts; 2 memory limits |
| XAI first-branch ablation | 0/20 | 11 resource limits; 8 timeouts; 1 memory limit |

These are descriptive counts for the observed cases only. **78 public-even records remain unscored**, and the audited final-odd claim set of 95 eligible records was not solver-scored. The frozen protocol's practical benchmark gate remains false; the 19-record original report and its disclosed resource-control deviations are preserved unchanged.

The stop decision changed project sequencing, not the experiment: the frozen protocol, freeze manifest, solver binaries, options, and historical rows were not modified to make the diagnostic look complete. The project may proceed to Phase 10, whose separate independent-reproduction blocker remains open. Any future full benchmark work is optional and requires a separately agreed time/resource budget; it does not block current Phase 10 progress.

## Artifacts

- [Original frozen 19-row diagnostic](Phase-9-public-even.jsonl) and [original partial summary](Phase-9-public-even-partial-summary-v2.json).
- [Stopped 20-row continuation record](Phase-9-public-even-stopped.jsonl) and [derived summary](Phase-9-public-even-stopped-summary.json); the summary retains `evaluation_complete: false` and `phase9_practical_gate: false`.
- [Frozen protocol](../TESTS/PHASE9_PROTOCOL.md), [freeze manifest](../TESTS/PHASE9_FREEZE.json), and [Phase 9 evidence report](Phase-9.md).
