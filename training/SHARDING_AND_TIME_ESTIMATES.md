# 19-rank sharding and time estimates

**Planning only.** This is an arithmetic estimate, not a training run, measured benchmark, approved data split, or claim that XAI is launch-ready. The user’s “19 hours” is treated here as a **19-hour total wall-clock target for both stages together**. The 19 workers are synchronized data-parallel ranks; the separate monitor is read-only and is not a twentieth training rank.

## How the 19 ranks divide the work

The selected UMR 2.0 candidate reports 210,237 sentences, 3,104,299 tokens, and 2,333,830 concepts in total. Dividing those published aggregate counts as evenly as possible gives this *illustrative* allocation:

| Rank | Candidate tokens | Candidate sentences | Candidate concepts | Illustrative 80% train tokens |
|---:|---:|---:|---:|---:|
| 0 | 163,385 | 11,066 | 122,834 | 130,708 |
| 1 | 163,385 | 11,066 | 122,834 | 130,708 |
| 2 | 163,385 | 11,065 | 122,834 | 130,708 |
| 3 | 163,384 | 11,065 | 122,833 | 130,708 |
| 4 | 163,384 | 11,065 | 122,833 | 130,708 |
| 5 | 163,384 | 11,065 | 122,833 | 130,708 |
| 6 | 163,384 | 11,065 | 122,833 | 130,707 |
| 7 | 163,384 | 11,065 | 122,833 | 130,707 |
| 8 | 163,384 | 11,065 | 122,833 | 130,707 |
| 9 | 163,384 | 11,065 | 122,833 | 130,707 |
| 10 | 163,384 | 11,065 | 122,833 | 130,707 |
| 11 | 163,384 | 11,065 | 122,833 | 130,707 |
| 12 | 163,384 | 11,065 | 122,833 | 130,707 |
| 13 | 163,384 | 11,065 | 122,833 | 130,707 |
| 14 | 163,384 | 11,065 | 122,833 | 130,707 |
| 15 | 163,384 | 11,065 | 122,833 | 130,707 |
| 16 | 163,384 | 11,065 | 122,833 | 130,707 |
| 17 | 163,384 | 11,065 | 122,833 | 130,707 |
| 18 | 163,384 | 11,065 | 122,833 | 130,707 |

These figures balance totals only; they are **not real shard files**. The published data is not approved for XAI training, and the actual corpus records, source groups, tokenizer lengths, graph-action lengths, and language proportions are not available. A true shard manifest must be made only after rights approval, annotation/mapping review, and leakage-safe train/dev/test assignment. Then keep each source and duplicate group wholly within one split and balance the *training* work across ranks by tokenized token count for pretraining and serialized graph-action count for graph training. Never split the raw files by line number and never shard dev/test into training.

In synchronized data parallelism, every rank participates in **every optimizer update** and receives a different local slice of that update’s global batch. Each rank starts from the same initialization/checkpoint, synchronizes gradients, and applies the same update. Rank 0 saves checkpoints. This is not 19 separately trained models, and rank runtimes do not add together: all ranks have the same wall-clock duration, determined by the slowest rank and synchronization. The monitor observes progress and does not train or mutate artifacts.

The reference configuration requests 8,192 pretraining tokens per global update and 4,096 graph actions per global update. Dividing by 19 gives averages of **431.16 token-equivalents** and **215.58 action-equivalents per rank per update**; actual batches are variable-length, so the implementation must balance valid tokens/actions and normalize losses correctly, rather than assume fractional examples. Every rank still completes all 200,000 pretraining updates and all 20,000 graph-training updates.

## Workload and time calculations

From [`reference.json`](reference.json), the full reference budget is:

- Pretraining: 200,000 updates × 8,192 tokens = **1,638,400,000 global token-equivalents**.
- Graph training: 20,000 updates × 4,096 actions = **81,920,000 global graph actions**.
- Total: **220,000 synchronized optimizer updates**.

If the entire published 3,104,299-token UMR candidate were used, the pretraining budget would consume about **527.8 corpus passes**. If an illustrative 80% training partition were used, it would be about **659.7 passes**. This demonstrates that UMR alone is much too small for the proposed pretraining schedule; repeating it does not create new data or establish language coverage.

For a 19-hour combined target, the no-overhead arithmetic requires an average of:

`(200,000 + 20,000) / (19 × 3,600) = 3.2164 global synchronized updates/second`.

That is an optimistic target if it excludes validation, checkpoint writing, startup, straggler time, network stalls, and recovery. If those consume a fraction `o` of the wall-clock budget, the required active rate is `220,000 / (19 × 3,600 × (1 − o))`. The estimator accepts an explicit `--overhead-fraction`; it defaults to zero rather than inventing an overhead value.

Actual runtime must use the measured synchronized update rates of the chosen 19-rank hardware. For pretraining rate `r_p` and graph-training rate `r_g`, the projection is:

`wall_hours = (200,000 / r_p + 20,000 / r_g) / 3,600 / (1 − overhead_fraction)`.

Rates must be **cluster-wide optimizer updates per second for all 19 ranks**, not single-GPU batch rates. Since the 19-rank environment does not exist on this computer, no measured rate is available and there is no defensible actual ETA yet. Example arithmetic only, before overhead:

| Measured pretraining rate | Measured graph rate | Pretraining | Graph training | Combined wall time |
|---:|---:|---:|---:|---:|
| 1 update/s | 1 update/s | 55 h 33 m | 5 h 33 m | 61 h 07 m |
| 2 updates/s | 1 update/s | 27 h 47 m | 5 h 33 m | 33 h 20 m |
| 4 updates/s | 2 updates/s | 13 h 53 m | 2 h 47 m | 16 h 40 m |

These examples are calculations, not benchmark results. Each of the 19 ranks has the same estimated elapsed time in each row; total accelerator consumption is 19 times wall time (for example, 19 hours of wall time uses 361 rank-hours, excluding the monitor).

## Reproduce or update the estimates

Run from the repository root; these commands only read JSON configuration and print JSON. They do not download data, create model weights, install packages, or start training.

```sh
python3 -m training.xai_train.estimate

# Replace these example rates with measured rates from the intended 19-rank cluster.
python3 -m training.xai_train.estimate \
  --target-hours 19 \
  --pretrain-updates-per-second 2 \
  --graph-updates-per-second 1 \
  --overhead-fraction 0.10
```

Omit either measured rate when it is unknown. The tool then reports only the available stage estimate and will not present a partial calculation as an end-to-end ETA. Its `rank_estimates` show count-balanced shares of the published candidate totals; they must not be mistaken for a prepared-data manifest.

## Remaining gates before any run

This estimate does not fill the gaps in the [execution plan](EXECUTION_PLAN.md). In particular, UMR use and model-artifact rights remain unresolved; the approved multilingual corpus, multiple accepted readings, reviewed graph mapping and byte alignment are missing; and the repository still needs a complete tokenizer, distributed trainer, checkpoint/restart, graph evaluator, export, and frozen-test runner. This computer has no PyTorch, SentencePiece, or CUDA GPU and only about 1.23 GiB free disk. A compatible 19-rank cluster and its rendezvous, storage, and recovery path also have not been provisioned. The read-only preflight remains blocked. No data preparation or training was performed for this estimate, and explicit user approval is still required before dispatching any future training run.
