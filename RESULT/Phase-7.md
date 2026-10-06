# Phase 7 — Decision policy, certificates, and abstention

**Phase 7 implementation gate: PASS (2026-10-07), for the in-process decision harness.** The new C++20 decision component validates state probabilities and action losses as exact GMP rationals, computes expected risk with an explicit resource penalty, admits only certificate-eligible actions, and keeps abstention available. The structured output reports the selected action ID/label/value and digest or abstention, exact risk/cost, per-action certificate status, upstream status, lineage references, assumptions, exactness, and limitations.

The chosen result value and risk/cost fields use the shared canonical `{"$rational":["numerator","denominator"]}` exact-number representation rather than floating-point JSON or an action-ID-only placeholder.

## Declared WMC policy

The supported action is `wmc.emit_exact_result`, which means returning an exact rational WMC result for a named formula; the alternative is abstention. Its declared state loss is zero for resolved state `exact` and one for any incorrect, incomplete, or unresolved state. The caller supplies the exact state probabilities; this module does not estimate them from the unlabelled benchmark. The probability distribution must sum to exactly one, and every candidate must supply one non-negative exact-rational loss per state. Missing losses, malformed or negative values, duplicate IDs/certificate IDs, requests above the 256-state/action cap, and invalid policy costs produce `invalid_input` rather than a fallback risk.

Risk is `sum(state_probability × state_loss) + resource_cost_weight × resource_cost`. The WMC policy convention uses a cost weight of 1 and an abstention cost of 1 normalized utility unit. Resource cost is `max(CPU_ms / 600000, peak_memory_bytes / 4294967296)`; values above 1 exceed the Phase 0 hard CPU/RAM budget and exclude that action. Ties among actions are resolved by smaller integer priority then lexicographic action ID; abstention wins an exact risk tie. These values are declared design conventions, not empirically fitted utilities. The default unresolved-mass allowance is zero; non-success upstream statuses and approximate/non-exact output under the exact-WMC policy lead to explicit abstention.

To bound work and output size, requests are limited to 256 states and 256 actions. State/action IDs and attached certificate IDs must be valid and unique within a request.

## Certificate gate

`wmc.emit_exact_result` requires property `xai.wmc.exact-rational-result.v1`. A certificate binds a typed certificate ID, action ID, formula SHA-256, canonical result SHA-256, property ID, verifier identity/version, and inclusive validity epochs. Missing, malformed, stale, mismatched, verifier-rejected, or verifier-error certificates make the action inadmissible. A resource overrun is rejected before any verifier call. If every candidate is inadmissible, the output is an explicit abstention with per-action diagnostics.

The verifier is an interface whose implementation receives the structured output value and must independently establish the named property; matching certificate metadata alone is insufficient. The interface supplies the formula digest but no formula content resolver. The test suite uses a synthetic verifier to exercise acceptance and rejection paths. **No production proof-certificate format, input resolver, or independent exact-WMC proof verifier exists here, so no real solver output is claimed certified.** The gate's evidence is about fail-closed decision mechanics only.

## Verification

A fresh GCC 13.3.0 / CMake 3.28.3 Release build passed CTest **9/9**. A separate fresh Debug build with AddressSanitizer and UndefinedBehaviorSanitizer also passed CTest **9/9**. The focused `xai_decision_tests` executable passed **8/8** groups in both builds:

- exact-rational risk ordering, certificate-before-ranking, and selected risk/cost;
- deterministic action tie-breaking and abstention-on-tie;
- malformed probability mass, missing state loss, duplicate certificate IDs, and request-size cap;
- absent, malformed, expired, and mismatched certificates;
- verifier rejection and thrown verifier exception;
- no-certified-action, resource-limit, and unresolved-state abstention;
- upstream timeout and approximate-output status propagation;
- canonical structured JSON and normalized CPU/RAM resource costs.

Build and reproduction commands plus focused transcripts are recorded in [`TESTS/validation.log`](../TESTS/validation.log). The component is covered by [`TESTS/cpp/decision_tests.cpp`](../TESTS/cpp/decision_tests.cpp); its interface and policy are specified in [`SPEC/components/06-output.md`](../SPEC/components/06-output.md).

These tests are bounded software checks, not corpus or empirical utility evidence. State probabilities, losses, lineage, and measured CPU/RAM usage are caller-supplied; the configured verifier and its soundness assumptions are trusted. No MCC archive outcomes or odd-indexed holdout bodies were used. This phase does not provide an end-to-end runtime, a production verifier, a benchmark-derived action threshold, or deployment evidence; orchestration remains Phase 8, and corpus evaluation remains Phase 9.
