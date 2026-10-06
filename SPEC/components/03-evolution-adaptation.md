# Component 3 — Evolution and Adaptation

## Phase 5 implementation

The Phase 5 harness in `include/xai/adaptation.hpp` and `src/adaptation.cpp` adapts one existing WMC solver setting. It does not add open-ended self-modification, a learned controller, a covariance-adaptation algorithm, or a production runtime.

| Item | Declared choice |
|---|---|
| Mutable parameter | `wmc::SolverOptions::node_limit`, owned by `xai_wmc` |
| Permitted values and hard bounds | `{1, 2, 4, 8, 16, 32, 64, 128}` recursive nodes per instance; inclusive numeric bounds `[1,128]` |
| Baseline | `node_limit=128`, version `wmc-node-limit-baseline-v1` |
| Candidate generator | Deterministic exhaustive scan of the eight-value ascending grid |
| Objective | Maximize the number of development cases solved exactly; ties choose the smallest node cap |
| Version format | `wmc-node-limit-v1-<limit>-run-<counter>` |
| Rollback | Any objective regression, oracle/invariant failure, streaming solver failure, or shift alarm freezes exploration and restores the versioned 128-node baseline |

Every other `SolverOptions` field is fixed for one controller. A `DevelopmentSuite` and every record must be labeled `development`, IDs must be valid and unique, and the whole suite is preflighted before any formula is evaluated. Each successful run must match its supplied exact count and satisfiability oracle. `resource_limit` outcomes count as noncompletions and expose no partial answer. The candidate score records completion count, denominator, recursive-node total for successful cases, and mean log-node count for successful cases. The baseline candidate's mean supplies the detector reference. Search cost is at most eight candidate evaluations per development record and every candidate has a 128-node hard ceiling; clause-scan work, rational arithmetic, memory, and wall time are not thereby bounded by a theorem.

The final-test path is fail-closed at the API boundary: a suite labeled `final_test`, or any non-development record in a development suite, is rejected before formula payloads are scored; the monitor accepts only `streaming` observations and rejects a `final_test` record before reading its solver result or node count. Partition labels are assertions supplied by the caller, not authenticated provenance. A caller that mislabels or leaks held-out information can defeat this in-process boundary; no locked benchmark instance was provided to or used by this harness.

## Residual monitoring and state transitions

For successful baseline solves on the first accepted development suite, let \(\bar{u}_0\) be the mean of \(\log(1+N_i)\), where \(N_i\) is the baseline recursive-node count. Baseline cases that do not complete are excluded from this descriptive reference. A streaming residual is

\[
r_t=\log(1+N_t)-\bar{u}_0.
\]

At the configured sample cadence, the one-sided Gaussian log-likelihood-ratio CUSUM assumes \(r_t\sim\mathcal N(\mu_0,\sigma^2)\) in control versus \(r_t\sim\mathcal N(\mu_1,\sigma^2)\) under an upward shift, with \(\mu_1>\mu_0\):

\[
W_t=\max\left(0, W_{t-1}+\frac{\mu_1-\mu_0}{\sigma^2}\left[r_t-\frac{\mu_0+\mu_1}{2}\right]\right),
\qquad \text{alarm when }W_t>h.
\]

Default detector settings are \(\mu_0=0\), \(\mu_1=0.5\), \(\sigma=0.25\), \(h=5\), and one monitored sample per streaming observation. The controller requires finite valid values, positive variance/threshold/cadence, and an explicit hold-off length (default three streaming observations). For fixed distributions, a higher threshold generally trades a lower false-alarm tendency for longer detection delay; a lower threshold trades faster alarms for more false alarms. These parameters define the assumed distributions and decision rule; the implementation makes no false-alarm rate or detection-delay guarantee. A solver failure, zero/out-of-range node count, or other monitored invariant failure freezes immediately. A CUSUM alarm restores baseline immediately. While frozen, development search is disabled; only valid streaming observations count down hold-off. After hold-off, an explicit reset clears CUSUM and restarts at baseline. Reset does not automatically resume a previously adapted setting.

Candidate ranking, exact development outcomes, monitor samples, alarms, freezes, resets, and version transitions are exposed in an append-only in-memory audit vector. The vector is not durable storage, authenticated provenance, or a cross-process log. Because the node cap changes which streaming cases complete and can censor high-work residuals, adaptation can change the residual distribution itself. The reference and Gaussian assumptions therefore need to be rechecked for each real integration; the detector is not a proof of system-wide stability or safety.

## Verification evidence and boundary

`TESTS/cpp/adaptation_tests.cpp` checks candidate bounds and ranking, exact-oracle agreement, final-test and mixed-partition rejection, duplicate-ID rejection, deterministic replay and audit output, regression rollback, invariant-failure rollback, streaming-failure rollback, cadence, a shifted-residual alarm, hold-off, explicit reset, and recovery from baseline. The suite uses small synthetic formulas and synthetic streaming signals. It validates finite software behavior only; it does not tune on the 98-instance Phase 0 development benchmark, open odd-indexed holdouts, establish WMC benchmark utility, validate the Gaussian model, or justify a false-alarm guarantee.
