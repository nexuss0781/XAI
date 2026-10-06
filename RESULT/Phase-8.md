# Phase 8 — Supported input mapping and end-to-end orchestration

**Gate: PASS (2026-10-07), for the synthetic in-process orchestration harness.** The accepted route is a caller-supplied structured, unprojected DIMACS-WMC record, pinned to a deterministic parser and solver. Each run records its stage trace, source/partition references, effective limits, versions, hashes, resource measurements, and canonical result or explicit failure/abstention. Parser-token details are sanitized from error reports; an absent or rejected certificate withholds the exact-count and satisfiability fields; a streaming alarm restores the controller’s baseline solver limit and prevents promotion of a result.

The manifest separately fingerprints the formula bytes, supplemental evidence/identity/annotations, model, configuration, code, and input/run/output schema descriptors. It records both the fixed parser version and producer/extractor version. Deterministic replay covers semantic outputs and exact adaptation-metric bits while excluding wall time, CPU, and RSS measurements. The report and machine-readable verification transcript are backed by the [orchestration contract](../SPEC/components/07-orchestration.md), [public API](../include/xai/orchestration.hpp), [implementation](../src/orchestration.cpp), and [end-to-end fixtures](../TESTS/cpp/orchestration_tests.cpp).

## Verification

A clean GCC 13.3.0 / CMake 3.28.3 / GMP 6.3.0 Release build passed the strict-warning configuration and all **10/10 CTest targets**. The focused orchestration executable passed **10/10 groups**, including multi-block SHA-256 vectors, successful certificate-gated WMC, partition rejection before formula hashing/parsing, unknown/inconsistent/correlated evidence, identity and unsupported-span handling, parser-token redaction, timeout, adaptation rollback, certificate and custom-memory-budget abstention, and deterministic replay.

A separate clean Debug build with AddressSanitizer, leak detection, and UndefinedBehaviorSanitizer also passed **10/10 CTest targets** and **10/10 orchestration groups**, with no sanitizer findings. Exact commands and results are recorded in [`TESTS/validation.log`](../TESTS/validation.log).

This pass establishes the orchestration contract and bounded software behavior only. All new orchestration fixtures are synthetic; the test verifier is synthetic; no Phase 0 archive formulas or solver outcomes were run; the public eligible subset has no new completion, performance, accuracy, or calibration result; and no odd-indexed final-test body was opened. The partition labels are caller-supplied rather than authenticated, and a production exact-WMC proof verifier is still absent. Task-specific evaluation remains Phase 9 work.

Implementation commit: [4e2c709](https://github.com/nexuss0781/XAI/commit/4e2c709).
