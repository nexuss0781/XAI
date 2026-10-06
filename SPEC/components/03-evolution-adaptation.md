# Component 3 — Evolution and Adaptation

## Purpose

Search a declared bounded parameter space for improved configurations and monitor residuals for change. “Evolution” means controlled optimization of explicit parameters; it does not imply open-ended self-modification, autonomous goal change, or guaranteed progress.

## Bounded search model

Let \(\theta\in\Omega=\{\theta:\|\theta\|_2\leq\Theta\}\), with objective \(J(\theta)\) measured only on a development set. A covariance-adaptation-style generation samples

\[
y_k=m+\sigma C^{1/2}z_k,\qquad z_k\sim\mathcal N(0,I),
\]

ranks candidates under the declared objective, and recombines the best candidates:

\[
m^+=\sum_{i=1}^{\mu}w_i y_{i:\lambda},\qquad w_i\geq0,\quad\sum_iw_i=1.
\]

A separate projected update can enforce parameter bounds:

\[
\theta^+=\operatorname{Proj}_{\Omega}(\theta+\eta g).
\]

Projection ensures membership in \(\Omega\) when correctly implemented; it does not establish system stability or safety.

## Change detection

For residual \(r_t\), a one-sided log-likelihood-ratio CUSUM is

\[
W_{t+1}=\max\left(0,W_t+\log\frac{f_1(r_t)}{f_0(r_t)}\right),\qquad
\text{signal when }W_t>b.
\]

The in-control and shifted distributions \(f_0,f_1\), threshold \(b\), sampling policy, and reset/hold-off behavior must be specified for each monitored quantity.

## Interface contract

Inputs include bounded parameter definitions, objective/version, development-data partition, residual stream, detector distributions, thresholds, and baseline configuration. Outputs include proposed parameter changes, objective values, bound status, detector state, and immutable version history.

## Assumptions and failure semantics

Optimization quality depends on the objective and search assumptions. Change-detection false-alarm and delay properties depend on residual distributions and sampling assumptions; adaptation may alter those distributions. On alarm, invariant failure, or unresolved objective regression, freeze exploratory changes and select the declared baseline. No update may exceed its explicit bounds.