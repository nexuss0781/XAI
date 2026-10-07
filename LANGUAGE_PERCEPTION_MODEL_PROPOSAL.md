# Learned multilingual text-perception model specification

**Scope:** this is a specification for a model that must be trained; it is not a trained model or an inference artifact. The repository currently contains a hand-written English rules prototype, not this model. No model weights, training corpus, training run, or learned-language results are claimed here. No simulated output, canned graph, synthetic training example, or rules-based semantic fallback is part of the specified model.

## Model and learning target

The target is one shared multilingual graph transducer. Given original text `x`, it learns a distribution over source-grounded semantic-hypothesis graphs `G`:

\[
P_\theta(G\mid x).
\]

The same trainable parameters `\theta` process every language. The declared locale is retained as input provenance but is not used to select a parser or alter semantic decoding. There are no language-specific meaning rules, language routes, prompt templates, imported pretrained checkpoint, retrieval source, or conversation context in version 1. A language can be claimed as supported only if it is represented by permitted training data and passes the held-out evaluation below. This does not promise competence in every language.

A graph is still a hypothesis about what the text expresses; it is not a verified fact, entity resolution, or an instruction to execute. The output remains the existing bounded candidate-graph record, with ordinal ranks and UTF-8 byte spans.

## One concrete architecture

### Input representation and shared network

Train a 32,000-piece unigram subword tokenizer with byte fallback on the permitted multilingual training partition. Byte fallback ensures that every valid Unicode string can be represented; it does not itself supply semantic understanding. Each token retains an exact interval in the original UTF-8 byte string. The tokenizer is frozen before model training and hashed with the model artifact.

Use one encoder-decoder Transformer initialized from a recorded random seed, not an external model checkpoint. The reference configuration is 8 encoder blocks and 8 decoder blocks, model width 512, 8 attention heads, feed-forward width 2,048, pre-layer normalization, GELU activations, dropout 0.1 during training, learned absolute position embeddings, standard scaled dot-product self/cross-attention, and a maximum of 1,024 input tokens. Initialize embeddings and linear weights independently from `Normal(0, 0.02²)`; initialize layer-normalization scale to 1 and bias to 0. The shared input embedding and all encoder/decoder weights are learned from the multilingual training data. Inputs longer than the configured limit fail explicitly in v1; they are not silently truncated or stitched together.

The encoder computes contextual states

\[
H=\operatorname{Enc}_\theta(z_1,\ldots,z_T),
\]

where `z_1,...,z_T` are tokenizer pieces for `x`. The decoder emits a typed sequence of graph actions `a_1,...,a_L`, with

\[
P_\theta(G\mid x)=P_\theta(a_{1:L}\mid x)
=\prod_{t=1}^{L}P_\theta(a_t\mid a_{<t},H).
\]

The action vocabulary includes graph/node start and end, node type and versioned concept label, edge relation, polarity/modality/speech-act attributes, source-span pointer, an explicit implicit-content marker, unresolved/unknown, and end-of-graph. A source pointer predicts token start `s` and end `e` with learned heads:

\[
P_\theta(s\mid H),\qquad P_\theta(e\mid s,H),\quad 1\leq s\leq e\leq T.
\]

The tokenizer alignment map converts those token boundaries to zero-based, half-open byte offsets in the unnormalized original input. A span that cannot be mapped exactly is rejected; the model must instead emit the explicit implicit marker or abstain. The decoder learns semantic labels and relations. A fixed output grammar may mask structurally invalid actions (such as an edge to a nonexistent node), but may not choose meanings, roles, or language-specific parses.

Before training, each annotated graph is serialized in a canonical order: nodes by first source-byte offset, then type and label; edges by source node, target node, and relation; attributes by their schema order. Candidate-local IDs are assigned from this order. Equivalent graphs therefore have one training serialization rather than an arbitrary permutation of node IDs.

## Training algorithm

There are exactly two learning stages, both using real, permissioned text. Initialize the model randomly before denoising pretraining; initialize graph training from those learned pretrained weights. Do not import an external checkpoint. The tokenizer and model see only the training partition. Documents and near-duplicates are grouped by source before partitioning, so development and final-test text cannot leak into tokenizer fitting or pretraining.

### 1. Multilingual denoising pretraining

Let `D_pre` be the licensed multilingual training text and `C_ρ(x)` a span-corruption operator that masks 15% of input tokens in contiguous spans with mean span length 3. The decoder reconstructs the masked token sequence `y` from the unmasked input. Optimize

\[
\mathcal L_{\mathrm{denoise}}(\theta)
=-\mathbb E_{x\sim D_{\mathrm{pre}},\,C_\rho}
\left[\frac{1}{|y|}\sum_{t=1}^{|y|}
\log P_\theta(y_t\mid y_{<t},C_\rho(x))\right].
\]

For balanced multilingual learning, sample a training language uniformly, then a document uniformly within that language, then a corruption span. This stage learns representations from multilingual text; by itself it is not evidence of semantic understanding.

### 2. Human-annotated graph training

For each natural text `x_i`, annotators provide the set `A_i` of distinct, materially acceptable graph readings. The annotation protocol covers ambiguity, spans, unresolved content, and disagreement; it does not force one interpretation where several are acceptable. A graph is linearized to its canonical action sequence `a^{(g)}_{1:L_{ig}}`.

Train against every accepted reading with equal target weight. The supervised objective is

\[
\mathcal L_{\mathrm{graph}}(\theta)
=-\frac{1}{|\mathcal L_{\mathrm{train}}|}
\sum_{\ell\in\mathcal L_{\mathrm{train}}}\frac{1}{N_\ell}
\sum_{i:\ell_i=\ell}\frac{1}{|A_i|}
\sum_{g\in A_i}\frac{1}{L_{ig}}
\sum_{t=1}^{L_{ig}}
\log P_\theta(a^{(g)}_t\mid a^{(g)}_{<t},x_i).
\]

Pointer decisions are actions in this sequence, so their start/end choices receive the same supervised log-loss as other graph actions. Equal weighting means “teach each accepted reading”; it is not an estimate of how often a reading is true. Decoder scores are never exposed as truth probabilities.

The reference experiment uses AdamW with `β₁=0.9`, `β₂=0.98`, `ε=10⁻⁸`, weight decay `0.01`, and global gradient-norm clipping at `1.0`. Pretraining uses peak learning rate `3×10⁻⁴`, 8,192 input/target tokens per update, and at most 200,000 updates; graph training uses peak learning rate `1×10⁻⁴`, 4,096 graph actions per update, and at most 20,000 updates. Both schedules warm up linearly for 2% of their update budget and then decay cosine-wise to zero. The reference seed is `20261008`. Select the graph checkpoint using development-set macro graph recall at 3; break ties by lower false-resolution rate, then lower exact-span error. Freeze this selection before running the final test. Record the actual data manifests, tokenizer/model/code hashes, software versions, and any deviation from these defaults. This is a reproducible starting configuration, not a claim that the values are optimal or that training has occurred.

## Inference without fabricated meanings

At inference, use constrained beam search of width 12 over graph-action sequences, discard only structurally invalid sequences, canonicalize and deduplicate identical graphs, and return at most the caller's limit (at most 3 in the reference configuration). Rank is the sequence score order only. Search may miss valid readings; the result declares itself non-exhaustive. No graph is inserted to fill an empty candidate slot. If there is no defensible graph, return a typed abstention; if the checkpoint or tokenizer is missing or invalid, return `model_unavailable`; if a limit is exceeded, return the corresponding explicit failure. There is no rules backend, canned answer, generated placeholder, or synthetic example fallback.

Deterministic code is allowed only for UTF-8/schema validation, token-to-byte alignment, action-grammar constraints, graph-reference checks, resource limits, canonical serialization, and reproducibility metadata. These operations enforce structure and provenance; they do not infer meaning.

## What would demonstrate learned language generalization

“Multilingual” is a measured transfer claim, not a property implied by the architecture name. For a zero-shot graph-transfer test, choose a target language `ℓ*` before the run. Its text may appear in the multilingual **unlabeled pretraining** partition, but none of its graph annotations may appear in graph training or checkpoint selection. Independent human annotations on untouched target-language documents form the final test. A few-shot or fully supervised result is a separate condition and must be labeled separately. A language with neither relevant pretraining text nor supervised examples is outside the demonstrated scope.

The test set contains only natural text. Report, per language and domain, graph recall at 3, a declared node/edge graph-matching score, exact byte-span accuracy, polarity/modality/speech-act accuracy, abstention, malformed-output rate, and latency/memory. On the predeclared ambiguous subset, report the proportion of examples for which the returned set omits a materially plausible annotated reading while presenting a narrower interpretation (false-resolution rate). Report macro-averages as well as per-language values so high-resource languages cannot hide failures elsewhere.

Run a paired ablation with the same architecture and graph labels but without multilingual denoising pretraining. The transfer difference is evidence about whether multilingual learning helped on this test; it is not proof of universal language competence. Do not train, select, or report semantic results on template-generated or synthetic examples. Simple hand-built fixtures may test only serialization and validator behavior, never model accuracy or language capability. Preserve the untouched final test and publish the data-use protocol, annotation agreement, split manifest, training configuration, checkpoint/tokenizer hashes, and failures with any capability claim. Do not emit calibrated probabilities in v1.

## Relationship to the current XAI code

The existing `TextObservation` and candidate-graph boundary can remain if they fit the trained decoder. Locale metadata stays in provenance and does not route to a rules parser. Existing UTF-8 byte-span and lineage requirements remain. A C++ inference adapter may load a fixed exported artifact only after parity, span-alignment, licensing, and resource tests; the model itself is trained and evaluated separately. The old `EnglishRuleBackend` is a legacy rules prototype, not this model, not evidence of learning, and not an acceptable fallback for it.

This document changes the design only. It does not install or train weights, supply a training corpus, change runtime behavior, or claim achieved accuracy. Until the specified training and held-out evaluation are actually run, there is no learned multilingual text-perception system to report as working.
