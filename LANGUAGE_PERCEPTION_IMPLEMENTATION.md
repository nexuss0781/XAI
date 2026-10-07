# C++ text-perception v1 implementation

The C++20 `xai_perception` library consumes either an accepted `xai::interaction::TextObservation`, a decoded shared `RecordEnvelope`, or the canonical JSON form of an exact `xai.interaction.text-input` v1 record. It returns a shared `RecordEnvelope` under `xai.perception.text-meaning-candidates` v1. It does not resolve real-world entities, assert facts, use prior conversation or external knowledge, or invoke the reasoning system.

## Component boundaries

- `InputValidator` checks required adapter provenance, UTF-8, declared locale, text size, payload fields, and the exact input schema/version.
- `SurfaceAnalyzer` tokenizes without changing the original text and records punctuation and quote cues using byte spans.
- `InferenceBackend` is a replaceable C++ interface. `EnglishRuleBackend` is the initial local baseline; it recognizes a small set of English predicates and explicitly preserves the two readings of “I saw the man with a telescope.”
- `GraphValidator` checks candidate-local identifiers, namespaced relations, node/edge limits, required grounding or an implicit marker, and source/cue spans on UTF-8 scalar boundaries.
- `Engine` routes only English declared-locale hints, applies limits, records ambiguity and provenance, and emits approximate results or typed fail-closed records.

The English locale is only a routing hint. No independent language detector is configured, so language identification remains unresolved in candidate records. Non-English and `und` locale inputs return `unsupported_input`. Unknown clause patterns abstain instead of returning an empty success. Candidate ranks are ordinal and no probabilities are emitted. Diagnostics do not contain input text, and the candidate record stores spans rather than copying the observation.

Default controls are 64 KiB input, 4,096 surface tokens, at most 3 candidates, 64 nodes and 128 edges per graph, a 1 MiB serialized result, a 16 MiB conservative peak-memory ceiling, and a 1,000 ms wall-time budget. Memory use is estimated from configured and actual component structures, not sampled from the process allocator. These limits are independently configurable within hard implementation caps. The bounded rules backend is not a general-purpose parser and is not evidence of broad language understanding or deployment readiness.

## Build and test

```sh
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The `xai_text_perception` CLI accepts a canonical text-input record as a file or from standard input and emits a canonical candidate-meaning record. For example, after creating an input record with `xai_text_adapter`:

```sh
xai_text_adapter --locale en-US input.txt > input-record.json
xai_text_perception input-record.json > meaning-record.json
```

An `approximate` result has one or more candidate graphs, explicit N-best/truncation metadata, the original observation/source lineage, and byte spans into the original unnormalized UTF-8 content. Non-result statuses carry typed errors and no result payload.
