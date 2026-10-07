# Learned multilingual text-perception model proposal

**Status:** design proposal only. No model has been trained, no model weights or training corpus are present in this proposal, and no current runtime output should be described as learned language understanding. This proposal specifies the intended replacement for the hand-written English rules backend; it does not implement it.

## Design decision

Replace language-dependent rules with a **shared multilingual Transformer encoder–decoder trained to map Unicode text to source-linked semantic-hypothesis graphs**. Language generalization must be an observed result of multilingual pretraining, graph-supervised training, and held-out evaluation—not a consequence of locale routing, a list of known words, or a hand-authored sentence template.

The model estimates a distribution over interpretations. It does not establish that an interpretation is true, resolve real-world entities, or execute instructions. The existing versioned `CandidateGraph` output is a suitable first output boundary: interpretations remain hypotheses, candidate rank remains ordinal, and source spans refer to the original UTF-8 bytes.

## What this replaces—and what may remain deterministic

The current `EnglishRuleBackend` recognizes a short list of English verbs, searches locally for a subject and object, and has a special case for “saw … with …”. It has no trained parameters and cannot generalize beyond the patterns encoded by its author. Its generated graphs are hand-authored templates, not AI model predictions. It must be removed from the production inference path when the learned backend is engineered; it must not silently remain as a fallback when model loading or inference fails.

Deterministic code remains appropriate for non-semantic duties: validating UTF-8 and record schemas, enforcing resource budgets, mapping model-tokenizer positions back to original byte spans, checking graph IDs and edge references, and serializing canonical records. These checks validate format and provenance; they must not decide that a phrase means a particular event or role. Synthetic fixtures may remain for contract and validator unit tests, but must never be presented as model training, inference, or language-capability evidence.

## Mathematical model

Let `x` be the original Unicode text, `m` the optional declared-locale metadata, `G` a candidate semantic graph, and `θ` the learned model parameters. The central inference target is

\[
P_\theta(G\mid x,m),
\]

where `m` is weak metadata, not proof of language. The model is multilingual and shares its learned parameters across the languages in its training data. There is no separate English rules branch and no rule-based language-to-meaning mapping.

### Shared encoder and graph decoder

A tokenizer with vocabulary learned from permitted multilingual training text maps `x` to subword units `z_1,\ldots,z_T`, retaining an exact alignment from each unit to its byte interval in the original input. A Transformer encoder computes contextual states

\[
H = \operatorname{Enc}_\theta(z_1,\ldots,z_T,m).
\]

The locale hint may be omitted, masked, or deliberately corrupted during training so the model learns not to rely on it as ground truth. The encoder is shared across supported languages; language-specific rule tables are not part of inference.

A Transformer decoder emits a typed sequence of graph actions `a_1,\ldots,a_L`, for example: create node, choose a node type and concept label, point to a source span, assign polarity/modality/speech-act attributes, add a typed relation between existing nodes, or stop. The graph probability factors autoregressively:

\[
P_\theta(G\mid x,m)
= \prod_{t=1}^{L} P_\theta(a_t\mid a_{<t},H).
\]

For a source span, learned pointer heads select the start and end tokenizer positions from encoder states. The recorded byte interval is recovered through the tokenizer’s alignment map. If the model uses a normalized working representation, alignment must still map back exactly to the original text; otherwise that span is invalid. A node with no defensible source span is allowed only when the model explicitly marks it implicit.

Graph action syntax and referential integrity may be constrained by the output schema (for example, an edge must refer to existing node IDs). These are structural constraints, not rules for interpreting language. The model predicts semantic labels from the versioned XAI vocabulary; labels it cannot support must be emitted as unresolved/unknown or cause abstention, not guessed into a familiar template.

A small learned language-evidence head may estimate `q_θ(ℓ | x)` for supported language labels. It reports uncertainty and can mark language unresolved. It does not choose a hand-written parser. Mixed-language input is supported only if the training annotations and evaluation explicitly cover it.

### Training objectives

Training has two distinct stages. First, multilingual denoising pretraining teaches shared text representations from permitted unlabeled corpora. For a corruption operator that masks spans of token sequence `z`, one suitable objective is

\[
\mathcal{L}_{\mathrm{pre}}(\theta)
= -\sum_{t\in M}\log P_\theta(z_t\mid z_{\setminus M},z_{<t}),
\]

where `M` is the masked-token set. This stage supplies learned linguistic representations; it does not by itself establish semantic-graph accuracy.

Second, supervised fine-tuning uses examples `(x_i, G_i, ℓ_i)`, where `G_i` is the set of human-accepted readings for input `x_i`, including multiple graphs when the text is genuinely ambiguous. The graph loss teaches every annotated reading rather than treating one arbitrary graph as uniquely correct:

\[
\mathcal{L}_{\mathrm{graph}}(\theta)
= -\frac{1}{N}\sum_{i=1}^{N}\sum_{g\in G_i} w_{ig}\log P_\theta(g\mid x_i,m_i),
\qquad \sum_{g\in G_i}w_{ig}=1.
\]

Weights are fixed by the documented annotation protocol (equal weights by default); they are not invented from raw model scores. Since each graph target contains learned source pointers, the loss trains semantic structure and alignment together. A language-evidence loss is optional where reliable labels exist:

\[
\mathcal{L}_{\mathrm{lang}}(\theta)
= -\frac{1}{N_\ell}\sum_i\log q_\theta(\ell_i\mid x_i).
\]

For human-verified parallel translations with equivalent meanings, a contrastive loss on pooled encoder representations can encourage cross-language alignment. For positive translation pairs `(x,x⁺)` and a batch of negatives `x⁻`, one form is

\[
\mathcal{L}_{\mathrm{align}}
= -\log\frac{\exp(\operatorname{sim}(u_x,u_{x^+})/\tau)}
{\exp(\operatorname{sim}(u_x,u_{x^+})/\tau)+\sum_{x^-}\exp(\operatorname{sim}(u_x,u_{x^-})/\tau)},
\]

where `u` is a pooled encoder representation, `sim` is cosine similarity, and `τ>0` is a tuned temperature. This term is optional and must only use verified semantic-equivalence pairs.

The fine-tuning objective is

\[
\mathcal{L}_{\mathrm{task}}
= \mathcal{L}_{\mathrm{graph}}
+\lambda_{\mathrm{lang}}\mathcal{L}_{\mathrm{lang}}
+\lambda_{\mathrm{align}}\mathcal{L}_{\mathrm{align}}
+\lambda_{\mathrm{reg}}\lVert\theta\rVert_2^2,
\]

with weights selected on the development set and then frozen before final testing. Parameters are learned by a declared optimizer, such as AdamW:

\[
\theta_{k+1}=\operatorname{AdamW}(\theta_k,\widehat{\nabla_\theta\mathcal{L}_{\mathrm{task}}},\eta,\beta_1,\beta_2,\epsilon,\lambda_{\mathrm{decay}}).
\]

The exact checkpoint, tokenizer, optimizer settings, data manifest, random seeds, training code revision, and training logs are part of the model artifact. They must be fixed and reported; none are claimed to exist yet.

## Data required for real language generalization

The training record is not just a sentence paired with one convenient answer. Each labeled example needs the original text, language label when known, one or more accepted graph readings, source-span annotations, and annotation/source provenance. Ambiguity-focused examples must include all readings the annotators judge materially plausible, such as attachment, scope, negation, modality, coreference, quotation, and ellipsis. Annotation instructions must define the graph vocabulary and how annotators mark unresolved or implicit content; disagreement is measured, not silently erased.

The corpus must include real, permissioned multilingual text for the target language set, graph annotations for each language being claimed, and verified aligned translations if the alignment loss is used. Unlabeled pretraining data can improve representations but cannot replace semantic graph labels for claiming semantic parsing. Fully synthetic or template-generated examples may be used only as explicitly labeled diagnostics and must not stand in for held-out natural-language evaluation.

Partition by source, document, and near-duplicate group before training. Freeze separate training, development, calibration (if probabilities are later needed), and untouched final-test sets. To substantiate transfer, include a predeclared language- or domain-held-out evaluation; report zero-shot, few-shot, and fully supervised conditions separately. Balance sampling or report macro-averages so a large-language corpus does not hide poor performance in smaller languages. Data licenses, collection permissions, and any privacy constraints must be recorded.

No model can honestly promise generalization to every language. The supported language/domain scope is the population actually represented in training and passed by held-out evaluation. A language absent from both pretraining and supervised data is not covered by the mere word “multilingual.”

## Inference and output behavior

At inference, encode the validated original observation and use deterministic beam search over valid graph-action sequences. The search returns at most the caller’s candidate limit; structurally identical graphs are deduplicated, and materially distinct candidates are retained when found. Each returned candidate is an approximate hypothesis. The model’s token-sequence log-likelihood is a search score, **not** a calibrated probability of truth or a guarantee that all readings were found. Candidate `rank` therefore remains ordinal in schema v1.

After decoding, deterministic validation checks the graph contract, candidate/node/edge limits, source-span boundaries, output size, and deadline. If model weights or tokenizer files are missing, their hashes do not match the manifest, inference exceeds budget, or the model cannot produce a defensible graph, return a typed `model_unavailable`, `resource_limit`, `timeout`, `unsupported_input`, or abstention result as appropriate. Never call the old rules, return a canned graph, or transform a failure into a successful-looking empty result.

Do not calibrate confidence from decoder likelihood. If probabilities are needed later, reserve independent calibration data and evaluate calibration and selective-risk behavior by language and domain. Otherwise emit no probability field.

## Fit with the current XAI contract

Keep the `TextObservation` input and candidate-graph output boundary where they remain sufficient. Reproducibility metadata should identify the model architecture/version, weights and tokenizer digests, training-data manifest, inference runtime, decoding configuration, and code build. Locale evidence should come from the learned language-evidence head (or remain unresolved), while the original declared locale remains separately preserved. The exact UTF-8 byte-span and provenance requirements in `LANGUAGE_PERCEPTION_DESIGN.md` continue to apply.

The current default limits (including 16 MiB estimated memory and a 1,000 ms wall-time budget) were set for a small rules backend. They are not evidence that a neural model can meet those budgets. Measure the actual model’s peak resident memory, latency, token throughput, and output size on the intended hardware; then set explicit serving budgets and return resource failures honestly. Do not shrink the model or fabricate outputs merely to preserve the old numbers.

A practical training/serving split is to train and evaluate the model in a reproducible ML training environment, export a fixed inference artifact, and load that artifact behind the existing C++ `InferenceBackend` boundary. The runtime choice (for example, an ONNX-exported model with a C++ runtime) must be selected by a numerical-parity, span-alignment, licensing, platform, and resource test before implementation. Model-vendor types and transport formats remain outside the XAI record schema.

## Evaluation gates before calling it learned language perception

A release candidate must be evaluated on untouched natural-language data, not only C++ unit tests. At minimum, report by language and domain: candidate graph recall at `K`, graph precision/recall or a declared graph-matching score, byte-span alignment, node/edge label quality, polarity/modality/speech-act accuracy, unresolved-ambiguity recall, and **false-resolution rate** (material alternatives hidden by the model). Report abstentions, malformed generations, unsupported inputs, latency, memory, and failures separately. Include challenge sets for ambiguity, multilingual/code-switch behavior if claimed, Unicode alignment, paraphrase, and distribution shift.

Compare against a relevant trained multilingual baseline and run ablations (for example, without multilingual pretraining, without parallel alignment, and with locale metadata removed). These comparisons establish whether the proposed training signals help. A surviving rules baseline may be run as a clearly labeled comparison, never as the target model or fallback. Pre-register target populations and release thresholds on development data, then report the locked final test without tuning against it.

A passing schema test means the record is well-formed. A successful training run means the optimizer completed. Neither establishes semantic competence. Capability claims require the held-out results, artifact hashes, data protocol, and failure rates. No broad or all-language claim follows from this design alone.

## Engineering consequence and non-goals

When implementation is authorized, replace the default `EnglishRuleBackend` with a trained-model backend and remove `perception_english_rules.cpp` from the production target. Reduce `SurfaceAnalyzer` to validation/alignment duties or replace its language-dependent tokenization with the learned tokenizer; it must no longer supply semantic evidence through hand-coded English cues. Add a separately reproducible training pipeline, corpus manifests, multi-reading annotation format, model artifact manifest, inference parity tests, and real held-out evaluation. Keep the public C++ facade and versioned result contract if they still fit the model.

This proposal does **not** train weights, select a legally cleared corpus, pick exact supported languages, claim achieved accuracy, promise unseen-language understanding, or alter source code. Until those items are completed and evaluated, the current rules backend remains only a legacy implementation and must not be represented as the requested learned model.
