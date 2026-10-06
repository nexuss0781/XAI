# Component 5 — Calibration

## Purpose

Measure probabilistic forecast quality and support uncertainty-aware set prediction and abstention. Calibration does not make a poor model correct and does not guarantee reliability under arbitrary distribution shift.

## Proper scoring rules

For forecast \(p\in[0,1]\) and binary outcome \(Y\), use strictly proper scores such as

\[
\operatorname{BS}(p,Y)=(p-Y)^2,
\qquad
\operatorname{LS}(p,Y)=-Y\log p-(1-Y)\log(1-p).
\]

Under the evaluation distribution, the expected proper score is minimized by reporting the true conditional probability. Report the score definition and the evaluation population; calibration and sharpness are distinct properties.

## Split conformal prediction

Given exchangeable calibration nonconformity scores \(R_1,\ldots,R_n\), use order-statistic index

\[
k=\left\lceil(n+1)(1-\alpha)\right\rceil.
\]

Use the \(k\)-th score in sorted order. When \(k>n\), the threshold is \(+\infty\); for the finite binary completion label space, this returns both possible labels (the full set). Under exchangeability between calibration and future examples, the result has marginal coverage at least \(1-\alpha\). It does not guarantee conditional coverage in every subgroup. Weighted conformal variants require their stated covariate-shift and density-ratio assumptions.

## Diagnostic outputs

Keep distinct: proper test scores; reliability-bin definitions and counts; conformal coverage and set size; risk-versus-coverage for abstention; simulation-based calibration where Bayesian inference code is used; posterior predictive checks; and shift indicators. No single scalar is a universal confidence guarantee.

## Data and interface contract

Calibration data must be disjoint from fitting and final testing according to the declared protocol. Return the method, nominal level, sample counts, scores/coverage, bin definitions, shift status, assumptions, and version identifiers. Label results as in-sample, calibration, or held-out evaluation results.

## Assumptions and failure semantics

If exchangeability or required shift assumptions are not defensible, do not report the corresponding coverage guarantee. Small samples, empty bins, missing outcomes, or detected shift are explicit limitations. Empirical calibration claims require calibration against data sampled according to the declared evaluation protocol.

## Phase 6 implementation

The in-process C++20 interface is `xai::calibration` in [`include/xai/calibration.hpp`](../../include/xai/calibration.hpp) with implementation in [`src/calibration.cpp`](../../src/calibration.cpp). A `CalibrationProtocol` versions the protocol, dataset, model, code build, calibration partition, selection policy, alpha, bin count, subgroup minimum, and record cap. Its fitting, calibration, and final-test ID lists must be pairwise disjoint; evaluated forecast IDs must match the calibration list exactly. Every forecast must carry that calibration partition and a finite probability in `[0,1]`. These checks enforce caller-supplied metadata, not authenticated provenance. The default maximum is 10,000 observations and reliability bin count is bounded to 1–100.

`evaluate()` returns distinct fields for mean Brier and unclipped logarithmic scores (with sample counts and normal-approximation standard errors/intervals when available), equal-width reliability bins with forecast/observed rates and pointwise Wilson 95% intervals, sharpness (forecast-probability distribution, mean confidence, and predictive entropy, including rows with missing outcomes), tie-aggregated risk-versus-coverage, subgroup counts and small-sample flags, shift-flag counts, and conformal threshold status. Reliability uses lower-inclusive/upper-exclusive bins except that the final bin includes `p=1`; the risk curve orders by `max(p, 1-p)`, predicts completion for `p >= 0.5`, and includes equal-confidence ties together. Caller-supplied subgroup and shift labels are descriptive only.

Split conformal is binary classification with nonconformity `1 - p(y)`. The API produces a threshold only when the caller provides a non-empty exchangeability basis; otherwise it returns `unsupported_assumption` and no prediction set. The `k > n` branch is tested and yields an infinite score threshold and the full binary label set. Calibration-set scores do not estimate held-out coverage, and no empirical coverage is emitted by the fixture report. No weighted conformal method is implemented. Simulation-based calibration and posterior predictive checks are not relevant to the current task because no posterior-sampling inference algorithm is implemented.

The frozen Phase 6 fixture is a version-controlled, hand-authored synthetic set (`phase6-synthetic-completion-fixture`, version `fixture-2026-10-07-v1`). It is only a software diagnostic fixture; it is not generated from the MCC archive, does not represent benchmark labels, and does not establish exchangeability. The measured fixture summaries, build evidence, and limitations are recorded in [`RESULT/Phase-6.md`](../../RESULT/Phase-6.md). The archive has no labels or calibration split; no odd-indexed holdout body is opened for Phase 6.
