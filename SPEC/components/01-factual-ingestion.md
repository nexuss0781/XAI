# Component 1 — Factual Ingestion

## Purpose and implemented scope

The Phase 2 implementation is the C++20 library in [`include/xai/ingestion.hpp`](../../include/xai/ingestion.hpp) and [`src/ingestion.cpp`](../../src/ingestion.cpp), built on the shared record contracts and GMP exact rationals. It represents a finite set of binary facts, an exact prior potential for each fact, and append-only evidence factors. It does not judge whether a source is trustworthy; source likelihoods and dependence labels must be supplied by a separately justified process.

The initial task adapter, `ingest_weighted_cnf`, maps the existing parsed, unprojected DIMACS-like WMC input into Boolean variable facts, literal-weight prior potentials, and deterministic clause factors. It is a bounded reference path for finite examples, not a replacement for the optimized WMC solver or a benchmark runner.

## Typed facts, identity, and world semantics

A fact has a stable `FactId`, a predicate token, and an explicit subject identity state. A resolved subject carries one `EntityId`; an unresolved subject carries no resolved ID and may carry candidate entity IDs. Candidate lists are retained as annotations only: this phase does not resolve identities, merge facts, or impose equality/inequality between candidates. A marginal is always about its `FactId`; query records say so when the subject remains unresolved.

The only implemented default is **open-world**. An identifier absent from the schema returns `unknown`, not false. A declared fact with no evidence still has its explicitly supplied prior marginal. No automatic closed-world rule or closed-world completion is implemented; any such task-specific rule must be encoded as explicit hard-constraint evidence over declared facts.

## Exact finite model

For each Boolean fact \(X_i\), the model stores nonnegative exact potentials \(w_i(0),w_i(1)\). They need not sum to one. The prior normalizer is

\[
Z_0=\prod_i (w_i(0)+w_i(1)).
\]

Each evidence record contains an ordered scope \(S_e\) and a nonnegative exact likelihood table \(L_e(x_{S_e})\). A hard constraint is a table containing only zero and one. The joint mass and evidence normalizer are

\[
W(x)=\prod_i w_i(x_i)\prod_e L_e(x_{S_e}),\qquad
Z_E=\sum_x W(x).
\]

The query returns the exact marginal \(\sum_{x:x_j=1}W(x)/Z_E\), plus \(Z_0\), \(Z_E\), and \(Z_E/Z_0\). For a weighted CNF, each clause is a deterministic factor, so \(Z_E\) is the exact WMC for the encoded formula. The normalized conditional marginal and the raw WMC are distinct outputs.

Prior and likelihood arithmetic uses GMP rationals only. Inputs are not rounded or renormalized. An all-zero prior has `zero_normalizer`; a zero evidence normalizer caused by hard constraints is `inconsistent`, while an all-zero soft-likelihood normalizer is `zero_normalizer`.

## Evidence and dependence policy

An accepted `EvidenceRecord` stores its evidence and source IDs, UTC observation time, original-observation reference, extractor version, exact likelihood/constraint semantics, ordered fact scope, dependency links, and data partition. The ledger copies accepted records, provides const access, and has no in-place edit or delete operation. A model accepts evidence only when its partition matches the model partition.

Dependence is never presumed independent. `independent` must be selected explicitly for likelihood factors. `model_factor` is reserved for a hard constraint whose product is part of the declared finite model; for example, multiplying CNF clauses expresses conjunction and is not a claim that clauses are independent corroborating sources. An `exact_duplicate` must link to an earlier record with the same scope and factor; it remains in provenance but is multiplied once. `correlated` and `unknown` records are preserved, but inference returns `unsupported_input` until an appropriate joint likelihood is supplied. This avoids silently multiplying dependent observations.

The CNF adapter canonicalizes literal order within clauses and links repeated clauses as exact duplicates. It records all clause evidence against the same source and original formula reference. No raw formula bytes are stored in the evidence record.

## Records, replay, and provenance

A model snapshot is a canonical `xai.factual_model` version 1 record. It contains typed facts, identity-resolution annotations, priors, all evidence and dependency links, partition, and resource limits. `BeliefModel::restore` validates and replays that record; a replayed query from the same state yields byte-identical canonical query output.

A query is a versioned shared `RecordEnvelope` with exact rational marginals and normalizers on success, or an explicit status and diagnostic with no numeric result on failure. It identifies the run, result, query, fact, model, query schema, code build, and partition. Its lineage lists the fact/entity annotation, model schema, build, every evidence ID, and each source ID. This supports provenance lookup within the serialized state; it is not an authenticated or access-controlled database.

## Limits and failure behavior

The default model caps are 63 declared Boolean facts, 1,048,576 enumerated worlds, 10,000 evidence records, 1,000,000 total factor-table entries, 50,000,000 counted exact-inference operations, and 512 bits per rational numerator/denominator and intermediate rational. The hard rational parser ceiling is 8,192 bits; per-model limits cannot exceed it. Shared canonical records also inherit the 16 MiB, 64-level, and one-million-value caps defined in [`SPEC/CONTRACTS.md`](../CONTRACTS.md). The world cap means the default exact query can enumerate at most 20 facts; the 63-fact representation ceiling does not promise that 63-fact inference will fit.

Every limit is fail-closed: a query or ingestion that exceeds it returns `resource_limit` without a partial marginal. Malformed types/tables/partition labels return `invalid_schema`; absent facts return `unknown`; unresolved or correlated dependence returns `unsupported_input`; a zero normalizer has the distinct statuses described above. No approximation method is implemented, so the system never silently changes an exact request into an approximation.

## Guarantee boundary

A successful result is exact for the encoded finite priors and factors, and its evidence/model/schema/build lineage is recorded. This does not validate the prior, likelihood, source quality, extractor, identity correspondence, or model-to-reality mapping. The component does not implement identity resolution, persistent storage, authentication, source trust estimation, or general-language extraction. The Phase 2 tests use small finite fixtures only; no Track 2 archive records or locked odd-indexed holdouts were consumed, and no corpus performance or Ganak comparison is claimed. See [`RESULT/Phase-2.md`](../../RESULT/Phase-2.md) for the gate evidence.
