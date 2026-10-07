# Repository Tools

These scripts are small, purpose-specific utilities. Run a script with `--help` before using optional arguments; the frozen Phase 9 utilities enforce protocol and overwrite safeguards.

- [`audit_wmc_archive.py`](audit_wmc_archive.py) — inspect the public portion of the 2024 MCC Track 2 archive and its input/weight contract.
- [`create_version_manifest.py`](create_version_manifest.py) — write a SHA-256 manifest for tracked source files and explicitly supplied inputs.
- [`phase9_eval.py`](phase9_eval.py) — implement the frozen Phase 9 audit and evaluation workflow. Consult [`../TESTS/PHASE9_PROTOCOL.md`](../TESTS/PHASE9_PROTOCOL.md) and verify the freeze before running it.
- [`phase9_repeat_case.py`](phase9_repeat_case.py) — wrapper for a single bounded public-even repeat; this is not a complete benchmark evaluation.
- [`grounding_train.py`](grounding_train.py) — prepare KILT T-REx and train/evaluate an offline exact surface-form slot-filling baseline; it does not implement a general-language parser or stable entity-ID linker.
- [`trainer.py`](trainer.py) — plan up to 20 disjoint KILT T-REx shards, build one count-index model per worker, and merge their additive sufficient statistics; it does not evaluate models.

Examples for discovering supported arguments:

```sh
python3 tools/audit_wmc_archive.py --help
python3 tools/create_version_manifest.py --help
python3 tools/phase9_eval.py --help
python3 tools/phase9_repeat_case.py --help
python3 tools/grounding_train.py --help
python3 tools/trainer.py --help
```

## KILT T-REx slot-filling baseline

The utility uses only the Python standard library. `prepare` downloads the official KILT-format train, dev, and answer-withheld test files, checks their frozen byte lengths, row counts, KILT record shape, split labeling, and SHA-256 hashes, and writes `data/kilt/trex/dataset-manifest.json`:

```sh
python3 tools/grounding_train.py prepare
```

`prepare` does **not** train. Training is a separate, explicit command that reads only the manifest-verified training split and writes an ignored SQLite model index:

```sh
python3 tools/grounding_train.py train \
  --data-dir data/kilt/trex \
  --model models/kilt-trex-slot-filling.sqlite
python3 tools/grounding_train.py evaluate \
  --model models/kilt-trex-slot-filling.sqlite \
  --data data/kilt/trex/trex-dev-kilt.jsonl \
  --output RESULT/kilt-trex-dev-evaluation.json
```

The baseline ranks answer surfaces by observed counts for exact normalized subject/alias and relation-label keys. Its candidate-share score is not a calibrated probability. KILT T-REx inputs are `subject [SEP] relation` prompts, not sentence-level mention examples, and the KILT records do not include stable Wikidata QIDs for all triple elements. The test file is answer-withheld and must not be used for labeled evaluation. See [`../TRAINING.md`](../TRAINING.md) for the intended claim boundary. No training or evaluation result is implied by preparing the script or data.

## Reusable sharded trainer

`trainer.py` plans disjoint shards, builds one SQLite index per shard, and synthesizes the additive result. `trainer_hub.py` transfers plans and models through Hugging Face Hub; it does not evaluate:

```sh
python3 tools/trainer.py plan --data-dir data/kilt/trex --run-dir data/kilt/trex/trainer-run --shards 20
python3 tools/trainer.py train-shard --run-dir data/kilt/trex/trainer-run --shard-index 0
python3 tools/trainer.py merge --run-dir data/kilt/trex/trainer-run --output-model models/kilt-trex-slot-filling-merged.sqlite
```

The manual [GitHub Actions workflow](../.github/workflows/trainer.yml) runs one plan job, a matrix with `max-parallel: 20`, then one merge job; each job has a 360-minute timeout. GitHub Actions artifacts are not used. Before dispatch, configure the existing Hugging Face repo ID and type as workflow inputs and add a write-scoped token as the GitHub Actions secret `HF_TOKEN`. The plan branch contains the manifest, shards, and merged SQLite file; 20 isolated worker branches preserve each partial SQLite model and avoid concurrent writes to one branch. These branches are intentionally retained and accumulate Hub storage until manually cleaned up. Merge downloads those partials one at a time and validates lineage before synthesis. The workflow stops after the merged model is uploaded—no evaluation or test stage runs. Check Hugging Face account storage and runner disk headroom first; see [`../TRAINING.md`](../TRAINING.md) for quotas, repo visibility, and disk guards. No training has been performed by adding this engine.

The Phase 10 reproduction harness lives in [`../TESTS/phase10_reproduce.py`](../TESTS/phase10_reproduce.py), alongside test and validation utilities. See [`../TESTS/README.md`](../TESTS/README.md) for its clean-build requirements.
