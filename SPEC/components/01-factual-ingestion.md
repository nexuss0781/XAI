# Component 1 — Factual Ingestion

## Purpose

Represent claims as typed facts with source lineage and uncertainty, then update a coherent belief state when new evidence arrives. This component does not decide whether a source is trustworthy by itself; source likelihoods and dependence assumptions must be supplied or estimated by a separately specified process.

## Mathematical state

For binary ground facts \(X=(X_1,\ldots,X_n)\), a possible world is \(x\in\{0,1\}^n\). A simple independent baseline is

\[
P_0(x)=\prod_i p_i^{x_i}(1-p_i)^{1-x_i}.
\]

When dependencies are represented by factors \(\psi_k\), use

\[
P_0(x)=Z_0^{-1}\prod_k\psi_k(x_{S_k}),\qquad
Z_0=\sum_x\prod_k\psi_k(x_{S_k}).
\]

An evidence record is \(e_t=(y_t,\ell_t,\tau_t,\nu_t,d_t)\), containing the observation, likelihood model, time, extractor/version, and provenance/dependence metadata. The Bayesian update is

\[
P_t(x)=\frac{P_{t-1}(x)L_t(y_t\mid x)}{Z_t},\qquad
Z_t=\sum_xP_{t-1}(x)L_t(y_t\mid x).
\]

A query \(Q\) returns \(P_t(Q)=\sum_{x\models Q}P_t(x)\), accompanied by evidence identifiers and model version.

## Input contract

The input must include a typed fact or bounded set of candidate facts; original observation bytes or a durable reference; source and time metadata; extractor version; likelihood or hard-constraint semantics; and dependency metadata linking duplicated or correlated sources. Entity-resolution uncertainty must remain explicit.

## Output contract

Return posterior marginals or a declared approximation, the normalizing status, evidence lineage, dependence assumptions, and model/schema identifiers. Exact enumeration is limited to finite tractable states; an approximation must identify its method and report an error bound when one is available.

## Assumptions and guarantees

Bayesian updating is mathematically defined when the prior and likelihood are valid and the normalizer is positive. Exact finite marginalization returns the exact result for the encoded model. These statements do not validate the prior, source likelihoods, extractor, or correspondence between the model and reality.

Tuple independence is an assumption, not a default truth. Duplicate evidence must not be counted independently without justification. Under open-world semantics, an absent tuple is unknown unless a specified prior or closed-world rule says otherwise.

## Failure semantics

If the normalizer is zero, return an inconsistency status; do not fabricate or silently renormalize a result. If evidence dependence is unknown, mark it unresolved and avoid claiming independent corroboration. If the state is too large for exact inference, return a supported approximation status or report that the query could not be evaluated.