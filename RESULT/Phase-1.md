# Phase 1 — Contracts and reproducible foundation

**Phase 1 gate: PASS (2026-10-07).** The repository now has shared, versioned C++20 record contracts; explicit result/failure rules; deterministic serialization and schema checks; reproducibility metadata; contract tests; a clean-build procedure; and a code/data/config version-manifest tool. A fresh strict-warning Release build and all registered tests pass.

## What was implemented

`include/xai/contracts.hpp` and `src/contracts.cpp` provide distinct identifier types for runs, observations, sources, evidence, facts, entities, models, schemas, code builds, partitions, queries, certificates, and results. The versioned envelope carries those identities, a data-partition label, machine-readable status, optional structured result, assumptions, typed lineage, exactness, resource budget and consumption, warnings/errors, and hashes/seed/algorithm metadata.

Statuses distinguish success, unsupported input, invalid schema, unknown, inconsistency, zero normalizer, timeout, resource limit, approximate output, non-identification, alarm/frozen, certificate rejection, and abstention. Successful records require a value; approximate records require an explicitly approximate value and warning; other statuses forbid a value and require an error. The decoder returns `invalid_schema` for malformed, noncanonical, or mismatched schemas rather than guessing or coercing. Budget overruns are rejected.

The shared wire format is canonical compact UTF-8 JSON. Object keys are byte-sorted, arrays preserve order, duplicate/unknown/missing fields and noncanonical spellings are rejected, and the parser has documented size/depth/node caps. JSON numbers are 64-bit integers only. Exact rational values use a schema-defined numerator/denominator string representation; the contract document defines reduction and denominator conventions. Envelope format and schema versions are separate, with exact-version matching and no implicit migration. Stable IDs, replay limits, numeric tolerance requirements, and restrictions on secrets and raw data in logs are documented in [`SPEC/CONTRACTS.md`](../SPEC/CONTRACTS.md).

`tools/create_version_manifest.py` hashes tracked source/document files and explicitly supplied data/configuration files, recording revision and dirty-tree signals without embedding file contents. Its instructions are in [`TESTS/README.md`](../TESTS/README.md). The manifest generator was exercised with a temporary data file and config file; both SHA-256 digests and byte counts matched independently computed values. The committed [`Phase-1-manifest.json`](Phase-1-manifest.json) pins the clean source/report revision and `CMakeLists.txt`; Phase 1 did not consume project data. The manifest intentionally does not hash itself, avoiding a self-referential digest.

## Validation evidence

A fresh build used GCC 13.3.0 and CMake 3.28.3 in Release mode with C++20 and strict warnings (`-Wall -Wextra -Werror`; WMC targets retain `-O3`). CTest passed **3/3** targets: the new contract suite, existing six-pillar fixtures, and exact-WMC tests. The contract suite passed **4/4** groups, including typed identifiers and partitions, malformed records, schema ID/version mismatches, canonical round trips, explicit unknown and all other failure states, invalid-enum rejection, numeric-format rejection, and resource-budget checks. The existing WMC suite passed **8/8** groups, including **67** weighted exhaustive-oracle cases; the six-pillar suite passed **6/6** groups. `git diff --check` passed.

A fresh AddressSanitizer/UndefinedBehaviorSanitizer build then passed CTest **3/3** with leak detection and UBSan halt-on-error enabled; the final contract validation changes were rebuilt before this run. No sanitizer findings were reported.

Reproduce the clean build from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The clean-build transcript and validation notes are recorded in [`TESTS/validation.log`](../TESTS/validation.log).

## Gate boundary

Phase 1 establishes an in-process exchange contract and deterministic serialization boundary; it does not add persistent storage, authentication, access control, a provenance database, or cross-process transport. Partition labels are represented and validated here, but later phases must implement authorization to prevent prohibited data use. The contract tests demonstrate software behavior on the stated cases, not task accuracy, benchmark performance, or end-to-end system capability.
