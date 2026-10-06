# Phase 9 public preflight (freeze v1; diagnostic only)

This is an interrupted public/even development preflight, **not** the completed Phase 9 evaluation. It must not be combined with the subsequent clean result stream.

| Evidence | Value |
|---|---|
| Frozen source revision | `0f9878727a400f338c3b6a70da120653d89090fa` |
| Frozen Ganak | v2.7.0, commit `e8f51841832efbbad4c1dc30e7e628ea39e8eec6` |
| Initial cross-split audit SHA-256 | `74a227bbd18116abf263951d288d4d33fcb0d1592ae4a1e55b82a3cc182a73b6` |
| Completed public records | Indices 0, 2, 4 (3 of 98 eligible) |
| Direct XAI/Ganak verifications | 0/3; no exact-count mismatch was observed. Ganak completed index 2 only; the full XAI configuration had no exact answer on these three records. |

The run stopped before index 6 was recorded because the harness eagerly computed and serialized `2^nvars × literal_occurrences` before checking the exhaustive-oracle cap. Public index 6 has 14,847 variables and 3,551,275 literal occurrences, yielding an integer estimate beyond Python’s 4,300-digit conversion limit. This was a harness serialization failure, not a solver count mismatch.

No final/odd solver scoring had started; odd members had only been opened for the predeclared cross-split duplicate audit. The fix short-circuits oracle-work estimation above the existing 20-variable cap and does not change solver arguments, eligibility, metrics, resource limits, or the oracle cap. The corrected code is being frozen and the cross-split audit repeated before any final-set scoring. Treat `Phase-9-public-even-preflight-v1.jsonl` as diagnostic evidence only; authoritative benchmark streams must use one consistent newer freeze and its corresponding audit.
