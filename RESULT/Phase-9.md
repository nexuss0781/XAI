# Phase 9 — Reduced public-even diagnostic

**Status: INCOMPLETE. Phase 9 gate: NOT PASSED.** At the user's direction, the frozen public-even run was stopped after the already-running index 36 finished, leaving **19 of 98** eligible public-even records (indices 0 through 36, even indices). No final-odd solver scoring was performed. This is a reduced diagnostic, not a completed Phase 9 evaluation; the original full-split exit and practical gates are not met.

The frozen solver binaries, source revision, protocol, and solver options were not changed. The run was pinned to one logical CPU and used the frozen XAI and Ganak settings. No tuning was performed on the observed results.

## Results on the 19 evaluated records

Ganak returned an exact count on **10/19** records. The XAI full configuration returned **0/19** exact counts; each of the three predeclared ablations also returned **0/19**. Therefore there were no XAI/Ganak pairs with two successful counts, no directly verified XAI count, and no estimable exact-match fraction among jointly completed pairs. No count mismatch was observed. The independent exhaustive oracle was eligible for **0/19** records under its frozen variable/work cap.

| System | Exact counts | Other recorded outcomes | Directly verified against Ganak |
|---|---:|---|---:|
| Ganak | 10/19 | 6 memory limit; 3 timeout | Baseline |
| XAI full | 0/19 | 11 resource limit; 6 timeout; 2 memory limit | 0 |
| XAI without unit propagation | 0/19 | 12 resource limit; 4 timeout; 3 memory limit | 0 |
| XAI without components | 0/19 | 13 resource limit; 4 timeout; 2 memory limit | 0 |
| XAI first-branch ablation | 0/19 | 11 resource limit; 7 timeout; 1 memory limit | 0 |

Across 95 solver attempts (19 records × five systems), the run recorded **10 exact completions, 47 resource-limit outcomes, 24 timeouts, and 14 memory-limit outcomes**. The absence of mismatches is limited to these attempted cases; it does not establish exactness or performance on the unrun records.

## Leakage audit and unscored final split

The already completed cross-split audit found 98 eligible public-even records and 97 eligible odd candidates. Odd indices **173** and **177** were excluded as near-duplicates of public index **176** (clause-set Jaccard 0.968085 each), leaving 95 eligible odd records in the audited final claim set. The audit had to decompress the odd formulas for the predeclared leakage checks; **no odd formula was solver-scored, used for tuning, or used to choose options** in this reduced run.

The synthetic WMC stress suite passed **11/11** checks. The frozen-build CTest suite passed **11/11** targets, and the Phase 9 Python harness tests passed **13/13**. These validate software and fixtures, not benchmark performance.

## Deviations and claim limits

This run intentionally stopped after index 36 rather than completing the 98-item public split, and the 95-item audited final split was not scored. The reduced prefix was not a predeclared confirmatory sample; do not use it for population inference or broad workload claims. Calibration, predictive scores, conformal coverage, and selective-risk measures remain not applicable because this counting archive has no outcome labels or calibration split.

The frozen per-attempt limits were 600 seconds wall time, one logical CPU, and 4 GiB resident memory, with 601/602-second soft/hard CPU limits. **Three recorded attempts exceeded even 602 seconds of measured wall time:** index 0 XAI first-branch (766,131 ms), index 0 XAI full (624,606 ms), and index 4 Ganak (814,576 ms). The harness classified all three as timeouts and discarded their outputs; they are disclosed as a resource-control deviation, not valid results. Fourteen attempts were classified as memory limits; the 10-ms RSS monitor recorded transient samples above 4 GiB, consistent with the protocol's stated sampling-overshoot caveat. The largest sampled RSS was 4,318,244,864 bytes.

Accordingly, **the Phase 9 gate is not passed**: the evaluation is incomplete, the XAI full configuration had no exact completion in the evaluated prefix, and resource-control overruns must be disclosed. This report does not convert the partial run into a full or positive benchmark result.

## Reproduction and artifacts

The 19 JSONL rows retain per-system command lines, output hashes, exact counts when present, statuses, wall/CPU/RSS measurements, and the frozen revision, freeze-manifest hash, and audit hash. The machine-readable partial summary records `evaluation_complete: false` and `phase9_practical_gate: false`.

- Frozen code revision: `a1a65b5ffdeab261bcdeaf7151c03b0c66213403`.
- Freeze manifest SHA-256: `b1b6dff0136f3e299a551d9074a862282347e2cb2aa8ecff161b543aaaaa1d12`.
- Cross-split audit SHA-256: `542b242966f631e42d65d34a242aa62df1d9d95342cf1b5073343787f6060901`.
- Partial public-even JSONL SHA-256: `7e83b7a28c8f6971db73593cf96e79f849aca70220e7b55bead25b303eef264e`.
- Partial machine summary SHA-256: `85e6ac6385cb0a7ed0630d113942548e05c331f2c91ecad2a9d07a36822bf304`.
- Frozen stress results SHA-256: `a8f67f4e01483afca530345de58e994c0e4016bb46460c63c0f202e4653b5ee7`.

The freeze manifest, locked protocol/code, and solver options remain unchanged. No Phase 9 completion claim is made beyond this reduced diagnostic.
