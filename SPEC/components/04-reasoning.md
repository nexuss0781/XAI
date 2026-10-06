# Component 4 — Reasoning

## Purpose

Evaluate bounded, typed questions using finite symbolic constraints, weighted model counting, optional causal models, or bounded program synthesis. It is not a general natural-language reasoner: a separate extraction layer must map tasks to a valid formal representation.

## Finite weighted model counting

Represent a task as \((H,w,E,Q)\): hard propositional constraints \(H\), nonnegative literal weights \(w\), evidence constraints \(E\), and query \(Q\). Define

\[
\operatorname{WMC}(F,w)=\sum_{x\models F}\prod_{\ell\text{ true in }x}w(\ell).
\]

When the denominator is nonzero,

\[
P(Q\mid E,H)=\frac{\operatorname{WMC}(H\land E\land Q,w)}
{\operatorname{WMC}(H\land E,w)}.
\]

A SAT solver may return a satisfying assignment or unsatisfiability result for supported finite propositional CNF. Exact WMC gives exact probabilities for the encoded finite model, but worst-case cost can be exponential.

## Causal queries

A causal request must name a structural causal model and query, such as \(P(Y\mid\operatorname{do}(X=x))\). Return a numeric answer only when the encoded graph, mechanism, and assumptions identify that quantity. Otherwise return `non_identified` or an explicit unsupported status. Observational association alone is not a causal identification argument.

## Bounded synthesis

An optional synthesis mode searches a finite grammar and bounded candidate space for a program satisfying an explicit specification. The result includes the candidate and verifier status. Search exhaustion or timeout returns `unknown`, not a proof that no solution exists.

## Interface contract

Inputs include typed variables, finite domains, hard constraints, weights, evidence, query, solver limits, and (for causal tasks) a declared graph/mechanism. Outputs include the status (`sat`, `unsat`, `probability`, `identified`, `non_identified`, `synthesized`, or `unknown` where applicable), a witness or result, assumptions, model version, and resource/approximation status.

## Guarantee boundary and failure semantics

Correctness applies only to the supplied formal model and solver implementation. Large or non-finite tasks require a separately justified method. A timeout, grounding failure, inconsistent constraints, zero WMC denominator, or unverified synthesis result must be explicit; none may be converted into a confident natural-language answer.