# Component 2 — Predictive learning

## Selected Phase 3 task and target

The initial predictive target is whether an eligible, parser-valid WMC instance produces an exact solver result within the predeclared project limits. `completed_exact` is a positive outcome; `explicit_noncompletion` is an observed explicit solver status without an exact count. Malformed or ineligible formulas, external process kills, and other runs with no trustworthy explicit solver outcome are `non_result` records and cannot be used as negative labels. This auxiliary prediction target does not change Phase 0's primary correctness and bounded-utility evaluation protocol.

The Phase 3 component is a reusable C++20 in-process harness. It has only been exercised on deterministic fixtures; it has not read the MCC archive, fit to benchmark outcomes, or produced a performance result. The Phase 0 even-indexed candidates remain development-only, and the odd-indexed holdout was not used for Phase 3 training or scoring. In the later Phase 9 protocol, odd-indexed formula bodies were decompressed for leakage checks only; no final-odd formula was solver-scored or used for tuning. Corpus training is not part of this phase.

## Versioned model library

The fixed library `library:wmc-completion-v1` has equal prior mass `1/3` on three binary experts. Each uses a Beta(1,1) predictive prior:

1. **Global Bernoulli:** one completion/noncompletion count pair over the authorized sequence.
2. **Structural-context Bernoulli:** one count pair per logarithmic structural bucket, keyed by `bit_width(variables)`, `bit_width(clauses)`, and `bit_width(literal_occurrences)`.
3. **Previous-outcome Bernoulli:** one count pair for each preceding outcome (`start`, `not_completed`, or `completed`).

The required feature schema is `xai.wmc_structure` version 1. The caller must set `phase0_eligible` only after the upstream WMC parser and exact-weight eligibility rule have accepted the record; the predictive component does not parse formulas or independently verify that assertion. All three counts must be present, nonnegative, and within the Phase 0 parser caps of 1,000,000 variables, 10,000,000 clauses, and 100,000,000 literal occurrences. Missing features, an unset eligibility assertion, and out-of-cap inputs return an explicit failure; they are never imputed. A previously unseen structural bucket receives its declared Beta(1,1) prior. The smoothed experts assign each binary outcome positive probability, so a zero-probability event cannot arise within this fixed library; malformed labels and non-results are rejected instead of receiving fallback certainty.

For expert \(h\), let \(p_{h,t}(y)\) be its Beta-Bernoulli predictive probability before outcome \(y_t\), and let \(w_{h,t}\) be the current mixture weight. The prediction is

\[
q_t(y)=\sum_h w_{h,t}p_{h,t}(y), \qquad
w_{h,t+1}=\frac{w_{h,t}p_{h,t}(y_t)}{q_t(y_t)}.
\]

The implementation computes mixture probabilities, weight updates, and cumulative log scores with log probabilities and log-sum-exp. It retains exact integer sufficient counts, per-expert cumulative predictive log scores, mixture log score, model weights, previous outcome, context counts, and the accepted observation IDs. The finite-mixture log-loss inequality is tested against each expert on fixtures and is a bound relative to this library and its prior—not a claim of absolute accuracy or coverage.

## Data-use and result contract

`predict()` is read-only and requires a named partition. It can produce a prediction for training, development, calibration, final-test, or streaming records, but has no outcome argument and cannot change state. `observe()` updates only on training or streaming records. Once streaming starts, training cannot resume. Development, calibration, final-test, `not_applicable`, and invalid feature/outcome update attempts return explicit failures and enter the training-use audit without retaining the supplied target when identity/partition metadata is valid. Calls missing the metadata required to identify an audit entry fail immediately and do not update state. `non_result` outcomes, including external kills and malformed attempts, are rejected and are not converted to noncompletion labels.

Successful predictions and updates use the shared versioned record envelope with the exact feature values, partition and observation IDs, three model probabilities and weights, sufficient-statistic summaries, cumulative scores, target when an authorized update was accepted, and SHA-256 hashes for the fixed model library and prior. Because the calculations use floating-point log probabilities, successful envelopes carry `approximate` status and identify the algorithm and limitation; failures carry no result. Model/prior hashes are fixed to the implementation, and arbitrary model IDs are rejected.

Canonical state snapshots preserve counts, per-model and mixture scores, the update audit, hashes, schema IDs, limits, and a numeric-runtime fingerprint. Restore validates schemas, hashes, limits, count consistency without unsigned overflow, context keys, audit-to-observation correspondence, and training-before-streaming order. Replay is byte-identical for the same implementation/runtime; snapshots with a different recorded compiler/standard-library/long-double signature are rejected. A matching signature is not a proof of identical math-library behavior. No persistence service, authentication, access-control system, concurrent writer support, or cross-runtime numeric equivalence is provided.

The default and hard maximum are 10,000 accepted examples, 10,000 audit entries, and 10,000 structural contexts. These caps bound in-memory state and keep ordinary canonical snapshots within the shared record layer's size/value limits; a caller may select lower limits but cannot raise them.

## Validation and claim boundary

The fixtures check known mixture probabilities and scores, the relative mixture bound, exact partition enforcement, missing and malformed inputs, non-result exclusion, duplicate observations, training-to-stream order, hash identity, versioned records, canonical replay/resume, forged snapshot rejection, and resource limits. The Phase 3 report records the clean Release and sanitizer evidence.

These checks establish only finite software and mathematical behavior for the declared library. No benchmark outcome has trained or tuned the model; no corpus accuracy, runtime prediction quality, calibration, generalization, or solver-performance claim is made. Any task-specific empirical evaluation remains subject to the frozen Phase 0 protocol and later Phase 9 work.
