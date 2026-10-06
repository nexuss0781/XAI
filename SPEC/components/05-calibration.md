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

Use the \(k\)-th score in sorted order; when \(k>n\), the finite-sample construction returns an unbounded prediction set. Under exchangeability between calibration and future examples, the result has marginal coverage at least \(1-\alpha\). It does not guarantee conditional coverage in every subgroup. Weighted conformal variants require their stated covariate-shift and density-ratio assumptions.

## Diagnostic outputs

Keep distinct: proper test scores; reliability-bin definitions and counts; conformal coverage and set size; risk-versus-coverage for abstention; simulation-based calibration where Bayesian inference code is used; posterior predictive checks; and shift indicators. No single scalar is a universal confidence guarantee.

## Data and interface contract

Calibration data must be disjoint from fitting and final testing according to the declared protocol. Return the method, nominal level, sample counts, scores/coverage, bin definitions, shift status, assumptions, and version identifiers. Label results as in-sample, calibration, or held-out evaluation results.

## Assumptions and failure semantics

If exchangeability or required shift assumptions are not defensible, do not report the corresponding coverage guarantee. Small samples, empty bins, missing outcomes, or detected shift are explicit limitations. Empirical calibration claims require calibration against data sampled according to the declared evaluation protocol.