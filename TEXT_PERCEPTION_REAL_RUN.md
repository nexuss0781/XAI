# XAI text-to-meaning: real CLI run

This walkthrough documents an actual run of the C++ text adapter followed by the C++ text-perception executable. The [raw terminal log](/workspace/xai-text-perception-real-run.log) contains the exact commands and complete, unedited JSON output.

## Input sentence

> I saw the man with a telescope.

The sentence has an attachment ambiguity: either the speaker used a telescope to see the man, or the man had a telescope.

## Commands run

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

The adapter emitted a successful `xai.interaction.text-input` v1 record. It recorded the sentence as 31 UTF-8 bytes, with locale `en-US` and `normalization: "none"`. The perception executable consumed that record and emitted `xai.perception.text-meaning-candidates` v1.

## What each of the five layers did

1. **Input validation** accepted the adapter record, UTF-8 content, locale, identifiers, and provenance without changing the text.
2. **Surface analysis** identified pieces of the sentence and anchored them to half-open byte ranges in the original UTF-8 text: `I` [0,1), `saw` [2,5), `the man` [6,13), and `with a telescope` [14,30). These are byte offsets, not character indexes.
3. **Inference backend** used the local `xai.perception.english-rules-v1` rules. It proposed two readings of the prepositional attachment and did not consult memory, retrieval, or external knowledge.
4. **Candidate-graph validation** checked candidate-local nodes and edges, namespaced relations, source spans, and graph limits.
5. **Serialization** emitted a versioned, provenance-linked result with status `approximate` and exactness `approximate`.

## Meaning candidates returned

**Rank 1** connects the “see” event to “with a telescope” using `xai:instrument`. In plain language, this means “I used a telescope to see the man.”

**Rank 2** connects “the man” to “with a telescope” using `xai:has-associated-modifier`. In plain language, this means “I saw a man who had a telescope.”

The record requested up to three candidates and returned two. The set was marked non-exhaustive, but not truncated; it also marked the attachment as unresolved. Candidate rank is ordinal only, not a probability, and neither reading is asserted as a fact about the world.

## Other important output notes

The output schema is `xai.perception.text-meaning-candidates` v1, and its algorithm field is `xai.perception.english-rules-v1`. The candidate record refers back to the source observation through its identifiers and lineage rather than copying the full text; its `source_content_included` value is false.

Language identification is also unresolved. `en-US` is the declared locale hint; this backend does not independently detect language. The record uses zero-based, half-open UTF-8 byte offsets into the original, unnormalized content.

## What the next layer can do

Semantic grounding can use additional context or evidence to resolve the attachment if possible. If the available context still does not decide between the readings, it should preserve both or ask a clarification instead of choosing arbitrarily.

## Limits of this demonstration

This is a real execution of the current local English rules backend on a pattern it recognizes. It demonstrates the record flow and ambiguity representation; it does not establish broad English coverage, general language understanding, or calibrated probabilities.