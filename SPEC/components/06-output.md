# Component 6 — Output and Abstention

## Purpose

Select a bounded answer/action under a declared loss and resource-cost model, while abstaining when that is safer or when required evidence/certification is missing. A certificate validates only an encoded property under its assumptions; it cannot establish that the formal property fully captures user intent.

## Decision model

Given belief \(q(\theta\mid h)\), candidate action \(a\), loss \(L(a,\theta)\), resource cost \(c(a)\), cost weight \(\lambda\), and abstention action \(\bot\) with cost \(\rho\), define

\[
R(a\mid h)=\mathbb E_{\theta\sim q}[L(a,\theta)]+\lambda c(a),
\qquad R(\bot\mid h)=\rho.
\]

Let \(\mathcal A_{\rm cert}\) be the actions whose required certificates pass the configured verifier. Select

\[
a^*=\arg\min_{a\in\{\bot\}\cup\mathcal A_{\rm cert}}R(a\mid h).
\]

If no action satisfies a required certificate, the admissible set contains only abstention.

## Interface contract

Inputs include the posterior or explicitly bounded belief state; candidate actions; state-dependent loss; resource costs; abstention cost; certificate policy; verifier identity/version; and task-specific output schema. Outputs include the chosen action or abstention, expected risk and cost, evidence lineage, assumptions, certificate status, and limitations.

## Assumptions and guarantee boundary

Bayesian decision theory minimizes expected loss only relative to the supplied belief and loss model. If either is misspecified, the selected action may be poor. A sound certificate proves only its specified property under its formal assumptions; it is not a proof of overall usefulness, factual grounding, or user-intent alignment.

## Failure semantics

Missing or invalid required certificates exclude actions. Invalid probability mass, unresolved high-risk uncertainty, unavailable loss values, or malformed inputs produce an explicit failure or abstention. The system must never present an uncertified action as certified or hide the assumptions behind a confidence number.