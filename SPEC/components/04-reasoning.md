# Component 4 — Bounded reasoning

## Supported finite task

The Phase 4 library answers typed finite-domain questions; it is not a natural-language reasoner. The caller supplies variables with finite, distinct value labels, hard constraints, optional non-negative exact weights, evidence assignments, and a query assignment. Variable identifiers follow the shared Phase 1 identifier contract. The current hard-constraint form is an explicit table of allowed tuples; symbolic, non-finite, or otherwise unsupported groundings return `unsupported_input` instead of being approximated.

Each variable may carry one exact rational weight per domain value. Omitted weights mean unit weights. Weights are non-negative but need not be normalized: the solver returns the exact weighted mass of evidence and query events and forms a conditional probability only by dividing by the evidence mass. This supports finite weighted model counting while preserving an explicit distinction between structural satisfiability and positive total weight.

For task variables \(X_1,\ldots,X_n\), hard constraints \(H\), evidence \(E\), query \(Q\), and unary weights \(w_i\), the implementation computes

\[
Z(E)=\sum_{x\models H\land E}\prod_i w_i(x_i),\qquad
Z(E,Q)=\sum_{x\models H\land E\land Q}\prod_i w_i(x_i).
\]

When \(Z(E)>0\), it returns the exact rational \(Z(E,Q)/Z(E)\). The empty query denotes the whole evidence event. An impossible query with positive evidence mass has probability zero.

## Results and limits

Results use the shared machine-readable status catalog. `success` includes exact numerator, denominator, structural SAT, and probability. `inconsistent` means no assignment satisfies both the hard constraints and evidence. `zero_normalizer` means at least one such assignment exists but its total weight is zero; exact zero masses are reported and no probability is returned. Malformed task structure yields `invalid_schema`; unsupported grounding yields `unsupported_input`; an expired time budget yields `timeout`; and exhausted structural/work budgets yield `resource_limit`. Timeout and resource-limit results contain no partial mass or probability.

Default limits are 64 variables, 10,000 constraints, 1,000,000 domain values, 256 bytes per symbol, 1,000,000 table rows, 1,000,000 table cells (sum of row arities), 1,000,000 complete assignments, 10,000,000 estimated operations, and a 10,000 ms deadline. A zero-millisecond deadline is already expired. Limits are configurable through `SolverLimits` in [`include/xai/reasoning.hpp`](../../include/xai/reasoning.hpp).

The reference algorithm checks the full Cartesian product of domain values. If \(S=\prod_i |D_i|\), \(C\) is the number of constraints, \(e\) and \(q\) are the evidence and query assignment sizes, and constraint \(j\) has scope width \(k_j\) and \(r_j\) allowed rows, worst-case enumeration time is \(O(S[n+e+q+\sum_j k_j\log(r_j+1)])\), in addition to arbitrary-precision rational arithmetic. Compiling the tables costs up to \(O(\sum_j r_j k_j\log r_j)\); storage is proportional to variables, domain values, and compiled table cells, with ordered-set overhead. The assignment, operation, table, symbol, and time caps bound the supported workload, but the operation estimate is not a bound on GMP bit complexity or resident memory. This is a small exact reference path, not an optimized SAT/WMC solver.

## Causal and synthesis policy

Causal reasoning remains separate from finite association/model queries. A request without a declared structural causal model, graph, mechanisms, or satisfied identification conditions returns `non_identified`. Even a request declaring all of those returns `unsupported_input` in this phase because no causal estimator or identification verifier is implemented; no intervention value is emitted. Caller-supplied flags are declarations, not proof that identification conditions hold. Observational association alone is never used to infer an intervention effect.

Bounded program synthesis is deferred. No synthesis result is produced, so search exhaustion cannot be confused with a proof of impossibility.

## Verification boundary

The implementation is in [`src/reasoning.cpp`](../../src/reasoning.cpp), and its independent recursive Cartesian-product oracle tests are in [`TESTS/cpp/reasoning_tests.cpp`](../../TESTS/cpp/reasoning_tests.cpp). These fixtures establish exact behavior only for tested typed finite tasks. They do not establish performance or accuracy on the MCC corpus, an optimized-solver advantage, causal identification, natural-language mapping, or end-to-end system capability.
