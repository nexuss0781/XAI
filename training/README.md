# Multilingual text-perception training pipeline

This directory implements the data-validation and split-preparation foundation for the train-from-scratch model in [`../LANGUAGE_PERCEPTION_MODEL_PROPOSAL.md`](../LANGUAGE_PERCEPTION_MODEL_PROPOSAL.md). It is not a trained model, a checkpoint, or evidence of language capability. No training is started by the tools in this change.

## Dataset selection

The selected initial graph-supervision candidate is **Uniform Meaning Representation (UMR) 2.0**, distributed by [LINDAT/CLARIAH-CZ](https://lindat.mff.cuni.cz/repository/items/239427de-bcaa-401d-a0ae-2c69602daa67). Its record reports 7,711 files, 210,237 sentences, 3,104,299 tokens, and 2,333,830 concepts across Arapaho, Chinese, Czech, English, Kukama, Latin, Navajo, and Sanapaná. The release and per-graphbank terms are summarized in [`datasets/umr-v2.json`](datasets/umr-v2.json).

This is a **data-source selection, not authorization to train**. The official [UMR 2.0 license](https://lindat.mff.cuni.cz/repository/static/license-umr-2.0.html) assigns different terms to individual graphbanks: several are CC BY-NC-ND 4.0, two are CC BY-NC-SA 4.0, and English is CC BY-SA 4.0. The repository does not presume that those terms permit training or release of a model artifact. Confirm data-use and model-output/weight terms for each included graphbank before preparing or training on it.

UMR 2.0 is the closest verified starting point for multilingual graph supervision, but it cannot alone satisfy the proposal. It contains only one graph per sentence rather than multiple accepted readings, only about 3.1 million tokens total for denoising pretraining, and its UMR graph/alignments do not directly equal XAI's hypothesis graph or its exact byte-span convention. The UMR data repository also marks partial conversions; those must not be treated as human gold without a separate audit. A reviewed graph mapping, exact source-byte alignment, and additional permissioned multilingual text (or a revised training plan) are needed.

## Input record format

The preparation API accepts validated JSONL records in the XAI training schema. Each record has a `record_id`, document-level `source_id`, BCP-47 `language`, original `text`, `text_origin` (`natural` or `human_translation`), `license_id`, `rights_status`, `use_pretraining`, `use_graph_training`, and `accepted_readings`. Each reading is a graph with typed nodes, relations, attributes, and zero-based half-open UTF-8 byte spans into the exact original text. A node without a source span must be explicitly marked `implicit`. Synthetic and template-generated records are rejected.

UMR release files are not silently converted to this schema. The dataset manifest records the specific adapter and rights work still needed, so no token-index alignment can accidentally be presented as a byte offset.

## Split preparation

With an independently reviewed and locally prepared JSONL file, generate stable source-group splits without training:

```sh
python3 -m training.xai_train.prepare \
  --input /path/to/approved-xai-records.jsonl \
  --output-dir /path/to/prepared \
  --seed 20261008 \
  --target-language xx
```

All records sharing a `source_id`, declared duplicate group, or normalized exact-text hash stay together. Development and test records are excluded from the pretraining split. If `--target-language` is supplied, target-language graphs are excluded from graph-training and checkpoint-selection partitions; target text is available for pretraining only when its entire group is in the training partition. The generated manifest stores file hashes, counts, split assignments, and target-language policy. The script rejects any record whose `rights_status` is not `approved`.

The data-preparation tests use small synthetic fixtures only to verify validation and split isolation. Those fixtures are never model inputs and never contribute to an accuracy or language-capability result.

## Readiness

Run the read-only gate:

```sh
python3 -m training.xai_train.preflight
```

The gate reports blockers and does not download data, install dependencies, initialize model weights, or start training. On the current computer the ML dependencies and GPU are absent and disk space is critically low; use an adequately provisioned training machine after rights and data readiness are addressed. A completed preflight is not itself evidence that training succeeded.
