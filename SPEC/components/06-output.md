# Component 6 — Decision, certificates, and abstention

## Phase 7 policy for the bounded WMC task

The available answer action is `wmc.emit_exact_result`: return an exact rational WMC result for the identified formula. `abstain` is always available. This phase implements the decision mechanics, not a full runtime that automatically constructs beliefs, actions, or proof certificates.

For declared states \(s\), the caller supplies exact probabilities \(p(s)\), and every action supplies a non-negative exact loss \(L(a,s)\) for every state. Probabilities must sum to one exactly. The action risk is

\[
R(a)=\sum_s p(s)L(a,s)+\lambda c(a),\qquad R(\bot)=\rho.
\]

For `wmc.emit_exact_result`, the declared loss is zero in the resolved `exact` state and one in every other supported incorrect, incomplete, or unresolved state. The exact-state probability is supplied by the caller; it is not estimated from the unlabelled benchmark by this module. The WMC policy uses \(\lambda=1\), \(\rho=1\) normalized utility unit, and a resource cost

\[
c(a)=\max\!\left(\frac{\text{CPU milliseconds}}{600{,}000},
                       \frac{\text{peak resident bytes}}{4\times 2^{30}}\right).
\]

These Phase 0 limits are hard caps: a normalized cost above one excludes the action. They are policy conventions, not fitted utilities or empirically estimated preferences. Ties between actions use the smaller declared integer priority, then lexicographically smaller action ID. Abstention wins a tie with the best action. The default unresolved-mass allowance is zero; if unresolved probability exceeds the explicitly configured allowance, the gate abstains before invoking any verifier. Any non-success upstream status, or non-exact output under the exact-WMC policy, also yields explicit abstention.

To bound decision work, a request supports at most 256 states and 256 candidate actions. State IDs, action IDs, and any attached certificate IDs must be valid and unique within their respective request scope; certificate IDs are unique across actions in one request.

## Certificate contract

An action that returns an exact WMC result requires property `xai.wmc.exact-rational-result.v1`. Its versioned certificate contains a typed certificate ID, action ID, SHA-256 of the input formula, SHA-256 of the canonical exact result, property ID, verifier ID/version, and inclusive validity epochs. A certificate is eligible only when all bindings match and it is current. Missing, malformed, expired/not-yet-valid, mismatched, rejected, or verifier-failure outcomes exclude the action. Resource-budget checks occur before verifier calls. If no admissible action remains, the policy abstains.

The `CertificateVerifier` interface receives the structured output value and both expected digests and must independently establish the named property; metadata matching by itself is not verification. The interface provides only the input formula's digest, so a production verifier would also need an authorized content-addressed input resolver or a separately defined proof bundle. Tests use a synthetic verifier only to exercise this gate. **There is no production WMC proof-certificate format, input resolver, or independent exact-count verifier in this phase**, so the tests do not establish that a real solver output has been certified. A future verifier must document its proof language, soundness assumptions, input coverage, implementation/version, and resource limits before real outputs can be described as certified.

## Output and failure behavior

`DecisionOutput` emits canonical JSON with schema ID/version, selected action ID/label/value and bound output digest (or `abstention`/`invalid_input`), exact expected risk and resource cost as reduced `{"$rational":["numerator","denominator"]}` objects, unresolved probability, per-action certificate/admissibility status, upstream status, exactness, lineage references, assumptions, diagnostics, and limitations. Exact results such as `3/8` are never converted to floating-point JSON numbers. Invalid distributions, negative values, missing losses, or malformed policy/input produce an explicit `invalid_input` record with no chosen action or fabricated risk.

Every lineage reference and probability/loss is caller-supplied and is not authenticated by this component. The verifier identity and soundness are trusted configuration. Expected-risk minimization is conditional on the supplied state distribution and loss model; neither is benchmark-calibrated. A certificate establishes only the encoded property under its verifier's assumptions, not usefulness, user-intent alignment, or safety of the broader architecture.

## Implementation and tests

The in-process implementation is in [`include/xai/decision.hpp`](../../include/xai/decision.hpp) and [`src/decision.cpp`](../../src/decision.cpp). The focused suite covers exact risk ordering, both levels of tie-breaking, malformed probability/loss input, missing/stale/mismatched certificates, verifier rejection and exceptions, no-certified-action, resource-limit and unresolved-state abstention, upstream status and exactness propagation, and canonical output. Evidence and remaining limitations are in [`RESULT/Phase-7.md`](../../RESULT/Phase-7.md) and [`TESTS/validation.log`](../../TESTS/validation.log).
