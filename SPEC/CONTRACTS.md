# Shared record contracts

Phase 1 adds a small C++20 contract layer at [`include/xai/contracts.hpp`](../include/xai/contracts.hpp), implemented in [`src/contracts.cpp`](../src/contracts.cpp). It is an interface and validation foundation, not a database, persistence layer, or full runtime. Components may embed task-specific structured values, but they share the same envelope and failure conventions.

## Record envelope and identifiers

Every record has an envelope-format version, a schema identifier and schema version, a machine-readable status, an optional structured result, assumptions, typed lineage, an exactness designation, a resource budget and measured consumption, warnings and errors, and reproducibility metadata. The identity block requires a run ID, result ID, schema ID, and code-build ID. It can also identify an observation, source, evidence item, fact, entity, model, data partition, query, and certificate. These are distinct C++ identifier types, so a `SourceId` cannot be assigned to a `RunId` by accident.

Identifiers are stable opaque ASCII tokens, 1–128 bytes, matching `[A-Za-z0-9][A-Za-z0-9._:-]{0,127}`. They must not contain secrets, personal information, or raw record contents. Producers choose IDs that remain stable across serialization and replay; this library validates their syntax but does not prescribe a global ID allocator or imply that the token itself proves identity. Lineage entries carry a typed ID and one of `derived_from`, `observes`, `cites`, `produced_by`, or `validates`.

The partition label is always explicit: `training`, `development`, `calibration`, `final_test`, `streaming`, or `not_applicable`. Named partitions require a `PartitionId`; `not_applicable` forbids one. This layer records labels but does not authorize model fitting or enforce Phase 3's update boundaries.

## Status and result rules

Status values are stable snake-case strings. The catalog distinguishes `success`, `unsupported_input`, `invalid_schema`, `unknown`, `inconsistent`, `zero_normalizer`, `timeout`, `resource_limit`, `approximate`, `non_identified`, `alarm_frozen`, `certificate_rejected`, and `abstention`. Diagnostics have their own machine-readable codes; human-readable messages add context but do not replace the status or code.

A `success` record must contain a structured result and cannot contain error diagnostics. An `approximate` record must contain a result, use `approximate` exactness, include at least one explanatory warning, and carry no error diagnostic. Every other status must omit the result, use `not_applicable` exactness, and include an error diagnostic. This fail-closed convention prevents unknown or failed computation from being represented as a default number. Resource consumption is checked against any declared budget. A successful record may still contain warnings.

## Canonical serialization and numeric conventions

The interchange format is compact canonical JSON, UTF-8 without a byte-order mark. Object keys are sorted by UTF-8 byte order; arrays preserve producer order. Strings use JSON escaping for quotes, backslashes, and control characters; standard short escapes are used where available, remaining control bytes use lowercase `\\u00xx`, and valid non-ASCII UTF-8 remains unescaped. Whitespace outside strings, duplicate keys, missing or additional envelope fields, noncanonical spellings, and invalid UTF-8 are rejected. The reader caps a record at 16 MiB, nesting at 64 levels, and one million JSON values.

JSON numbers are signed or unsigned 64-bit **integers only**. Decimal points and exponent notation are forbidden, including for approximate measurements. Exact task values that exceed integer range or require fractions must use a schema-defined exact representation. The shared convention for a rational is an object of the form `{"$rational":["numerator","denominator"]}`: both members are canonical base-10 integer strings, the denominator is positive, the pair is reduced by its greatest common divisor, and zero is represented as `0/1`. Domain-specific schema validators remain responsible for checking those rational constraints. Approximate outputs must identify their algorithm and tolerances in their task schema and use the approximate status; floating-point bit patterns must not be silently serialized as JSON decimals.

`encode_record` validates before writing. `encode_canonical_value` also serializes a typed canonical value under the same UTF-8, depth, and size limits, but deliberately does not apply a task-specific schema; the Phase 7 decision serializer validates its outcome fields before using it. `decode_record` rejects malformed, unsupported, or noncanonical envelopes as `invalid_schema`; when an expected schema ID/version is supplied, a mismatch returns an explicit `invalid_schema` failure rather than a partially decoded record. No automatic coercion or schema guessing occurs.

## Versioning, provenance, and reproducibility

Envelope format version and task-schema version are independent positive integers. The current envelope format is version 1. Readers require exact schema compatibility. Additive, breaking, or semantic schema changes require a documented version increment; old records are never reinterpreted in place. A migration is an explicit, separately versioned program that preserves the original bytes, records source and destination schema versions, records its code-build ID and inputs, emits a new result ID, and retains lineage to the original record. No implicit migration is implemented in this phase.

Reproducibility metadata records the algorithm name, metadata format version, optional seed, and named SHA-256 hashes for code, data, configuration, or other inputs. Hash names are stable labels, and digests are lowercase 64-character hexadecimal strings. A project-level manifest can be generated with `tools/create_version_manifest.py`; see [test and reproduction instructions](../TESTS/README.md). The manifest hashes tracked source files and any explicitly supplied data/config files; it records metadata and hashes only, never embeds their contents.

Deterministic replay means that the same canonical inputs, partition, schema, code/data/config hashes, algorithm, seed, and declared limits reproduce the same semantic result. Exact deterministic algorithms should reproduce byte-identical canonical output. Stochastic or floating-point algorithms must state their seeds and algorithm-specific absolute/relative tolerances in the task schema. Wall-clock timings, scheduler behavior, memory availability, external interruptions, and unpinned dependency/runtime differences are not promised to replay identically; a resource-limit or timeout result remains explicit.

## Logging and retention boundary

Ordinary diagnostics and logs must not contain secrets, restricted raw data, credentials, private keys, or full input records. Use stable record IDs, field paths, status/error codes, and content hashes instead. Raw observations and source artifacts remain under their own permission, retention, and access controls; this library neither stores nor deletes them. Hashes aid integrity checks but do not make restricted data public or anonymous.

## Phase 1 boundary

The shared types establish an exchange format and local invariants. They do not provide persistent storage, signature/authentication, privacy guarantees, identity resolution, access control, a provenance database, cross-process transport, or enforcement of later-phase data-use rules. These contracts do not demonstrate task accuracy or end-to-end system behavior.
