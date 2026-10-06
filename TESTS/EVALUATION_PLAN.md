# Evaluation Plan (Future Work)

No real-dataset experiment has been run. This plan is a checklist for a later, task-specific evaluation—not evidence that the system works.

## Define the task and data

Before training or inference experiments, define the task, target population, data schema, preprocessing, deduplication policy, labels, outcome timing, and allowed input/output. Inspect the actual corpus: record count, duplicates, label quality, coverage of cases, source dependence, noise, and machine-readable fraction. A 1 GB file is not a sufficiency metric.

Freeze a training split, a development split, a separate calibration split where required, an untouched final test split, and a temporal or domain-shift test. Prevent entity, source, or near-duplicate leakage across splits when it would invalidate the evaluation.

## Comparisons and ablations

Compare simple task-appropriate baselines with the full system and targeted ablations: without provenance tracking; with independent-evidence assumptions versus dependency-aware updates; without adaptation; without abstention; and under distinct memory/resource budgets. Keep the data and tuning budget comparable.

## Measures

Report task-specific accuracy or utility; proper probabilistic scores and calibration; conformal coverage only under its assumptions plus prediction-set size; selective risk versus coverage; retrieval/provenance fidelity; contradiction and entity-resolution errors; runtime, peak memory, stored-state size, I/O, and approximation status/error. Include confidence intervals, all failures, seeds, software versions, data versions, and the exact protocol.

## Stress cases

Include copied reports, correlated sources, entity-resolution mistakes, contradictions, temporal drift, missing evidence, out-of-domain inputs, solver timeouts, and corrupted or incomplete records. Record whether the system abstains or reports an explicit failure.

## Claim boundary

A successful result can support only a bounded performance claim for the named task, dataset, implementation, and evaluation conditions. It does not establish general intelligence or novelty. The mathematical guarantees apply only when their formal assumptions are met.