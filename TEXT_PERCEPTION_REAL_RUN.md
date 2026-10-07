# Text perception CLI: a real run

This walkthrough follows an actual run of the C++ text adapter and text-perception executable. The [complete terminal transcript](TEXT_PERCEPTION_REAL_RUN.log) includes the commands and unedited JSON output.

> **At a glance:** The local English rules backend returns two readings of an ambiguous sentence. The attachment remains unresolved, and candidate ranks are not probabilities.

## Input

> I saw the man with a telescope.

**Why it is ambiguous:** The speaker may have used a telescope to see the man, or the man may have had a telescope.

## Reproduce the run

```sh
printf '%s' 'I saw the man with a telescope.' > /tmp/xai-example-replay.txt

./build/xai_text_adapter \
  --locale en-US \
  --source-id source:example \
  --observed-at 2026-10-08T00:00:00Z \
  /tmp/xai-example-replay.txt \
  > /tmp/xai-input-record-replay.json

./build/xai_text_perception \
  /tmp/xai-input-record-replay.json \
  > /tmp/xai-meaning-record-replay.json
```

> **Note:** This run used the existing local executables in `./build`. The adapter emitted a successful `xai.interaction.text-input` v1 record; the perception executable consumed it and emitted `xai.perception.text-meaning-candidates` v1.

The input record preserves the sentence as 31 UTF-8 bytes, with locale `en-US` and `normalization: "none"`.

## What happened in each layer

1. **Input validation** accepted the adapter record, UTF-8 content, locale, identifiers, and provenance without changing the text.
2. **Surface analysis** identified parts of the sentence and anchored them to half-open byte ranges in the original text: `I` `[0,1)`, `saw` `[2,5)`, `the man` `[6,13)`, and `with a telescope` `[14,30)`. These are byte offsets, not character indexes.
3. **Inference** used the local `xai.perception.english-rules-v1` rules to propose two readings. It did not consult memory, retrieval, or external knowledge.
4. **Candidate-graph validation** checked candidate-local nodes and edges, namespaced relations, source spans, and graph limits.
5. **Serialization** emitted a versioned, provenance-linked result with status and exactness both marked `approximate`.

## Meaning candidates

1. **Rank 1 — telescope as the instrument:** The `xai:instrument` relation connects “with a telescope” to the “see” event. In plain language: “I used a telescope to see the man.”
2. **Rank 2 — telescope as a modifier of the man:** The `xai:has-associated-modifier` relation connects “with a telescope” to “the man.” In plain language: “I saw a man who had a telescope.”

> **How to read the ranking:** The run requested up to three candidates and returned two. The set is non-exhaustive but not truncated, and the attachment is explicitly unresolved. Rank is ordinal only—not a probability—and neither reading is asserted as a fact about the world.

## Output notes

- **Source linkage:** The candidate record refers to the source observation through its identifiers and lineage instead of copying the full text; `source_content_included` is `false`.
- **Language:** `en-US` is a declared locale hint. Language identification remains unresolved because this backend does not independently detect language.
- **Text offsets:** Spans use zero-based, half-open UTF-8 byte offsets into the original, unnormalized content.

## What the next layer can do

Semantic grounding can use additional context or evidence to resolve the attachment. If the available context still does not decide between the readings, it should preserve both or ask for clarification rather than choosing arbitrarily.

## Scope and limits

This is a real execution of the current local English rules backend on a pattern it recognizes. It demonstrates the record flow and ambiguity representation; it does not establish broad English coverage, general language understanding, or calibrated probabilities.
