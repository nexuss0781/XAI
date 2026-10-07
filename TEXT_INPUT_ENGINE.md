# XAI text-input engine

This component is XAI’s interaction-layer adapter for **plain text**. Its C++ API accepts UTF-8; its command-line adapter accepts UTF-8, UTF-16, or UTF-32 and stores the content as canonical UTF-8. It validates Unicode text and provenance, then returns a typed C++ observation or a versioned XAI record. It does not interpret the text: receiving a sentence is not the same as understanding its meaning.

## What happens to an input

```text
UTF-8/16/32 file or stdin / UTF-8 C++ caller
                  │
                  ▼
   bounded Unicode decoding and validation
                  │
                  ▼
    UTF-8 TextObservation (no normalization)
                  │
                  ▼
    versioned XAI record (canonical JSON)
                  │
                  ▼
       future perception / grounding stage
```

The adapter preserves Unicode content without trimming, Unicode normalization, line-ending conversion, translation, tokenization, fact extraction, entity resolution, task inference, or invocation of the existing WMC solver. The C++ API preserves accepted UTF-8 bytes exactly. The CLI transcodes UTF-8/16/32 source bytes into canonical UTF-8; a leading byte-order mark (BOM) is treated as an encoding signature and removed. No valid Unicode scalar value is filtered: this includes NUL, C0/C1 controls, formatting characters, combining sequences, and supplementary-plane characters. JSON serialization escapes required control bytes, and decoding the record restores the original UTF-8 content. Ill-formed UTF-8/16/32, unpaired UTF-16 surrogates, and out-of-range Unicode values are rejected.

Text content is opaque. Markdown, HTML, CSV, JSON, XML, source code, and other textual formats can be passed through without parsing or rewriting. This is not a binary-document reader: containers such as DOCX or PDF, images, audio, and other non-text formats must first be converted by the caller into Unicode text.

Input limits are measured in bytes, not characters. The default limit is 1 MiB; callers can select another positive limit up to the 4 MiB hard ceiling. The CLI bounds both source bytes and decoded UTF-8 content against the selected limit. The shared canonical JSON record limit is 32 MiB, which accommodates worst-case JSON escaping of text near the 4 MiB input ceiling.

## Why the adapter stops at the boundary

The repository’s existing XAI implementation has bounded factual ingestion and formal reasoning components, but it does not implement a raw-language perception or semantic-grounding system. Sending arbitrary prose directly to those components would silently invent structure they cannot currently derive. This engine therefore provides a reliable handoff point: original text, a declared locale, source and observation identifiers, capture time, and an explicit statement that the content has not been interpreted.

The output uses the shared record envelope with schema `xai.interaction.text-input`, version `1`. Its structured result includes `content` in UTF-8, its UTF-8 byte count, locale, media type, timestamp, opaque input reference, and `normalization: "none"`. Typed lineage connects the record to the observation and source. Downstream perception or grounding can consume the C++ `TextObservation` directly, or read the versioned record.

## Build and run

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Read a file and emit one canonical JSON record:

```sh
./build/xai_text_adapter --locale en-US --source-id source:terminal prompt.txt
```

The CLI detects UTF-8, UTF-16LE/BE, or UTF-32LE/BE when a BOM is present. To read BOM-less UTF-16/32, select the encoding explicitly:

```sh
./build/xai_text_adapter --encoding utf-16le prompt.txt
./build/xai_text_adapter --encoding utf-32be prompt.txt
```

`--encoding auto` is the default: it detects a BOM and otherwise treats the input as UTF-8. Available explicit values are `utf-8`, `utf-16le`, `utf-16be`, `utf-32le`, and `utf-32be`. A BOM that conflicts with an explicit encoding is rejected.

Read from standard input without placing text in a command-line argument:

```sh
printf '%s' 'Explain how XAI receives text.' | \
  ./build/xai_text_adapter --locale en --source-id source:terminal --encoding auto -
```

For production callers, provide stable observation and result identifiers, an appropriate source identifier, an opaque `--original-ref`, and the actual observation time with `--observed-at YYYY-MM-DDTHH:MM:SSZ`. The CLI generates convenience IDs and a current UTC timestamp when these are omitted; those defaults are suitable for local use, not a substitute for an application’s durable ID allocator or source-capture clock. `--max-bytes N` sets the source-byte and canonical UTF-8 content limits up to 4 MiB.

The JSON record contains user text by design. Apply the caller’s access and retention policy, and do not write it to ordinary diagnostic logs. The adapter’s rejection diagnostics do not echo input content.

## C++ interface

Include `xai/text_input.hpp` and link the `xai_text_input` CMake target. The C++ API accepts UTF-8 and preserves its bytes:

```cpp
#include "xai/text_input.hpp"

xai::interaction::TextInputMetadata metadata;
metadata.run_id = xai::contracts::RunId{"run:chat-42"};
metadata.result_id = xai::contracts::ResultId{"result:chat-42-1"};
metadata.observation_id = xai::contracts::ObservationId{"observation:chat-42-1"};
metadata.source_id = xai::contracts::SourceId{"source:user"};
metadata.code_build_id = xai::contracts::CodeBuildId{"build:local"};
metadata.original_input_ref = "conversation:42/message:1";
metadata.observed_at_utc = "2026-10-07T00:00:00Z";
metadata.locale = "en";

const xai::interaction::TextAdapter adapter;
auto received = adapter.receive("Hello, XAI.", std::move(metadata));
if (!received.accepted()) {
    // Use received.status and received.diagnostic; raw text is not echoed.
    return;
}
const auto& observation = *received.observation;
const std::string canonical_json = xai::interaction::serialize(observation);
```

For a named data partition, callers must provide its `partition_id`; otherwise the default is `not_applicable`. The adapter rejects missing or malformed provenance rather than filling in unsupported claims about where the text came from.

## Tests and guarantees

`xai_text_input_tests` covers byte-for-byte preservation, multilingual and supplementary-plane Unicode, combining sequences, formatting characters, all classes of control characters, canonical JSON escaping and round-trip, malformed and truncated UTF-8, overlong forms, surrogate and out-of-range code points, metadata validation, and configured/hard size ceilings. The CLI encoding test covers BOM detection and explicit UTF-16/32 decoding, malformed sequences, encoding conflicts, and Unicode-content preservation. The CLI reads inputs incrementally and stops at the configured source-byte ceiling instead of buffering an unbounded stream.

These are software-interface checks, not evidence that XAI understands language. The implementation does not provide a language model, speech recognition, entity resolution, task framing, memory storage, authentication, persistence, or a natural-language-to-facts converter. A later component that interprets text must produce uncertain, provenance-linked hypotheses and keep them distinguishable from the original observation.
