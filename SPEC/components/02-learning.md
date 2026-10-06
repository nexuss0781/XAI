# Component 2 — Learning

## Purpose

Adapt predictions from a declared library of probabilistic models by updating model weights as data arrive. Learning here means sequential predictive updating over the selected library; it is not unrestricted concept formation and does not guarantee that useful models are present.

## Mathematical model

Let \(\mathcal H\) be a declared finite or countable model library with prior mass \(\pi(h)>0\). For observations \(z_1,\ldots,z_n\), the one-step predictive mixture is

\[
q(z_t\mid z_{<t})=\sum_{h\in\mathcal H}w_t(h)p_h(z_t\mid z_{<t}),\qquad
w_t(h)=\frac{\pi(h)p_h(z_{<t})}{\sum_j\pi(j)p_j(z_{<t})}.
\]

After observing \(z_t\), update

\[
w_{t+1}(h)\propto w_t(h)p_h(z_t\mid z_{<t}).
\]

Under logarithmic loss, for any fixed \(h\) in the declared library,

\[
\sum_{t=1}^{n}-\log q(z_t\mid z_{<t})\leq
\sum_{t=1}^{n}-\log p_h(z_t\mid z_{<t})+\log\frac1{\pi(h)}.
\]

The inequality follows because the mixture probability assigned to the sequence is at least \(\pi(h)\) times the probability assigned by \(h\).

## Candidate model library

A task-specific library may include categorical and finite-state models, conjugate Gaussian models, regularized linear or logistic models, and small decision trees. The library, priors, feature schema, and update policy must be versioned and fixed before final evaluation.

## Input and output contracts

Inputs are validated examples with schema, labels/outcomes where available, data-use partition, and sequence metadata. Outputs include predictive distributions, updated model weights, sufficient statistics, cumulative log score, model-library version, and an explicit status for unknown or unsupported inputs. Compute log probabilities with numerically stable log-sum-exp methods.

## Assumptions and guarantee boundary

The mixture regret bound is relative to the declared library and chosen prior mass. It does not guarantee absolute predictive accuracy, coverage of the target process by the library, or robustness to arbitrary distribution shift. Any additional generalization bound must state its data and loss assumptions and prior-selection procedure.

## Failure semantics

Reject malformed or out-of-schema examples with an explicit status. Do not update on final test data. If every model assigns zero probability, report model misspecification or invalid input rather than silently injecting certainty. Under detected shift, follow the system-level freeze/baseline policy.