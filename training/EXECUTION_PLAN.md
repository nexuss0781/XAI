# Text-perception Transformer execution plan

**Status:** planning only. No dataset has been prepared for this run, no model weights exist, and no training job has been started. This plan records the intended topology; it is not a runnable workflow or authorization to begin training.

## Model boundary

Train one from-scratch multilingual encoder-decoder Transformer for **text perception only**. Its output is the existing bounded, source-grounded set of candidate-meaning graphs, with explicit ambiguity, provenance, and UTF-8 byte spans. The model proposes what an observation may mean; it does not declare those meanings to be facts.

The Transformer does not replace or absorb XAI's other layers. Those remain downstream and separately governed: semantic grounding, evidence/provenance handling, cognitive state and memory, world modelling, formal and probabilistic reasoning, prediction, adaptation, calibration, decision and permission gates, and orchestration. They consume typed, versioned perception records. Entity resolution, factual acceptance, reasoning, planning, and actions are not training targets for this perception model and are not silently entrusted to it.

Integration therefore means connecting the exported perception model through the typed text-perception interface—not averaging its parameters with unrelated XAI components or turning all of XAI into one neural model. The existing English rule backend is a legacy prototype, not a trained-model fallback.

## Intended 19 + 1 topology

Use **19 synchronized data-parallel training ranks** for each training stage and **one separate, read-only admin monitor**. Each training rank starts from the same model state, receives a distinct portion of each global batch, participates in gradient synchronization before the optimizer step, and advances the same model/optimizer state in lockstep. This is a single distributed training run, not 19 independent models.

The denoising-pretraining stage begins from the recorded random initialization. Graph-supervised training then continues from the selected pretraining checkpoint. At each synchronized step, ranks should have equivalent global parameters; rank 0 writes the authoritative checkpoint. After development-set selection, export one model package containing the selected weights, tokenizer, configuration, and provenance manifest. **There is no post-hoc weight merge.** Averaging checkpoints trained independently on separate datasets is not the specified algorithm and is not an acceptable substitute for synchronized gradient updates.

The admin monitor is outside the 19 optimizer ranks. It may inspect run state and non-sensitive progress metrics, but it must be read-only: it does not train, modify artifacts, or count as a model shard.

These are 19 synchronized workers, not 19 independent training runs with separate finish times. Each rank participates in all global optimizer updates, processes a distinct local share of each global batch, and waits for gradient synchronization; all ranks have the same wall-clock duration, governed by the slowest rank. The read-only monitor is a 20th process, not a training rank. The published UMR aggregate-count division is only an estimate; actual source-group-safe, token/action-balanced rank shards can be created only from rights-approved data after leakage-safe train/dev/test splits.

The estimate-only calculator is `python3 -m training.xai_train.estimate`. It computes 19-way aggregate count shares and the update throughput required for a selected wall-time target. With measured cluster-wide updates/second for both phases, it estimates stage and total wall time. It does not benchmark, create data shards, initialize weights, download data, or start training. See [SHARDING_AND_TIME_ESTIMATES.md](SHARDING_AND_TIME_ESTIMATES.md) for the 19-hour arithmetic and sample calculations. No actual duration is available until the intended 19-rank environment is measured.

Nineteen ordinary isolated CI matrix jobs do not by themselves create a distributed training cluster. The run requires a provisioned environment with 19 compatible compute ranks, a shared rendezvous/network path for gradient synchronization, adequate accelerator memory and storage, and restart/checkpoint handling. A CI workflow may act as a control plane only after that distributed backend is selected and tested. Do not dispatch the current KILT T-REx matrix workflow for this model: it builds additive SQLite answer counts, not Transformer weights.

## Gates before any training run

All of the following must be satisfied before a run is even considered ready:

1. **Rights and data:** identify permitted text and annotation sources, document allowed uses and model-artifact terms, and obtain required rights confirmation. The current UMR candidate is explicitly blocked pending rights review; do not download, prepare, or train on it as though its use were approved.
2. **Supervision and conversion:** select a sufficiently sized permissioned multilingual pretraining corpus; define a reviewed graph annotation protocol that permits multiple acceptable readings; implement and audit any source-to-XAI graph mapping and exact byte-span alignment; group sources and near-duplicates before splitting.
3. **Training/evaluation implementation:** implement and test the tokenizer, complete trainer, synchronized 19-rank gradient path, checkpoint/resume and export, development selection, and frozen held-out evaluation. Current repository code is a foundation, not this end-to-end runner.
4. **Compute and storage:** provision and verify the distributed environment, dependencies, accelerator topology, capacity, and recovery behavior. The current computer lacks PyTorch, SentencePiece, and a visible CUDA device and has only about 1.2 GiB free disk, so it is not a training host.
5. **Preflight and authorization:** run the read-only readiness checks against the chosen data and compute environment. Even if all gates pass, wait for the user's explicit approval before dispatching any training job.

Until then, safe work is limited to design, rights/data readiness, implementation, and non-training tests. Tests may verify schemas, split isolation, tensor shapes, synchronization mechanics on controlled test fixtures, and artifact validation; they must not be represented as model training or evidence of language capability.

## Current known state

The `pro` branch contains the model specification, a reference Transformer class, objective helpers, strict training-record validation, split preparation, a read-only preflight, and an estimate-only rank/time calculator. It still does not contain a full tokenizer, synchronized trainer, checkpoint/resume path, complete evaluator/export runner, or trained checkpoint. Its selected UMR candidate manifest records unresolved per-graphbank rights, one graph per sentence, too little text for the proposed pretraining schedule, and missing conversion/evaluation work. The preflight reports blockers; training remains unstarted. A run must not be described as end-to-end ready until those implementation, data, rights, and compute gates are closed and the complete workflow is validated on the intended cluster.
