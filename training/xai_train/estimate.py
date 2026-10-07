"""Estimate 19-rank data shares and training wall time; never launches training.

Throughput inputs are measured *global synchronized optimizer updates per second*
for the full 19-rank cluster, not per-rank examples/second. Without measured
rates, only arithmetic workload and the rate required for a time target are
reported; no runtime prediction is invented.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REFERENCE = ROOT / "training/reference.json"
DEFAULT_DATASET = ROOT / "training/datasets/umr-v2.json"
WORLD_SIZE = 19
DEFAULT_TARGET_HOURS = 19.0


def _balanced_counts(total: int, parts: int) -> list[int]:
    """Divide an aggregate integer as evenly as possible, low rank IDs first."""
    if total < 0 or parts <= 0:
        raise ValueError("total must be non-negative and parts must be positive")
    quotient, remainder = divmod(total, parts)
    return [quotient + (rank < remainder) for rank in range(parts)]


def _nearest_fifth(total: int) -> int:
    """Round 80% to the nearest whole item (half up), for a rough split estimate."""
    return (total * 4 + 2) // 5


def _positive_rate(value: float | None, name: str) -> float | None:
    if value is not None and value <= 0:
        raise ValueError(f"{name} must be greater than zero")
    return value


def estimate(reference: dict[str, Any], dataset: dict[str, Any], *,
             target_hours: float = DEFAULT_TARGET_HOURS,
             pretrain_updates_per_second: float | None = None,
             graph_updates_per_second: float | None = None,
             overhead_fraction: float = 0.0,
             world_size: int = WORLD_SIZE) -> dict[str, Any]:
    """Return reproducible workload arithmetic and optional measured-rate ETAs."""
    if target_hours <= 0:
        raise ValueError("target_hours must be greater than zero")
    if world_size <= 0:
        raise ValueError("world_size must be positive")
    if not 0 <= overhead_fraction < 1:
        raise ValueError("overhead_fraction must be in [0, 1)")
    pretrain_rate = _positive_rate(pretrain_updates_per_second, "pretrain_updates_per_second")
    graph_rate = _positive_rate(graph_updates_per_second, "graph_updates_per_second")

    counts = dataset["published_counts"]
    pretraining = reference["pretraining"]
    graph_training = reference["graph_training"]
    pretrain_updates = int(pretraining["max_updates"])
    graph_updates = int(graph_training["max_updates"])
    pretrain_tokens_per_update = int(pretraining["tokens_per_update"])
    graph_actions_per_update = int(graph_training["actions_per_update"])
    pretrain_total_tokens = pretrain_updates * pretrain_tokens_per_update
    graph_total_actions = graph_updates * graph_actions_per_update
    candidate_tokens = int(counts["tokens"])
    candidate_train_tokens = _nearest_fifth(candidate_tokens)
    target_seconds = target_hours * 3600.0
    total_updates = pretrain_updates + graph_updates
    required_rate = total_updates / (target_seconds * (1.0 - overhead_fraction))

    rank_rows = []
    raw_per_rank = {
        "published_tokens": _balanced_counts(candidate_tokens, world_size),
        "published_sentences": _balanced_counts(int(counts["sentences"]), world_size),
        "published_concepts": _balanced_counts(int(counts["concepts"]), world_size),
        "approx_train_partition_tokens_at_80pct": _balanced_counts(candidate_train_tokens, world_size),
    }
    for rank in range(world_size):
        rank_rows.append({
            "rank": rank,
            "data_share_estimate": {name: values[rank] for name, values in raw_per_rank.items()},
            # Every DDP rank executes every synchronized update. These are local
            # token/action-equivalents per update, not a fraction of optimizer steps.
            "local_pretraining_token_equivalents_per_update": pretrain_tokens_per_update / world_size,
            "local_graph_action_equivalents_per_update": graph_actions_per_update / world_size,
            "synchronized_pretraining_updates": pretrain_updates,
            "synchronized_graph_updates": graph_updates,
        })

    stage_estimates: dict[str, Any] = {}
    if pretrain_rate is not None:
        stage_estimates["pretraining"] = {
            "measured_global_updates_per_second": pretrain_rate,
            "updates": pretrain_updates,
            "estimated_compute_hours": pretrain_updates / pretrain_rate / 3600.0,
        }
    if graph_rate is not None:
        stage_estimates["graph_training"] = {
            "measured_global_updates_per_second": graph_rate,
            "updates": graph_updates,
            "estimated_compute_hours": graph_updates / graph_rate / 3600.0,
        }
    # Only publish a combined ETA when both phases have measured rates; a missing
    # phase must never disappear silently from what looks like an end-to-end ETA.
    if pretrain_rate is not None and graph_rate is not None:
        compute_hours = sum(stage["estimated_compute_hours"] for stage in stage_estimates.values())
        stage_estimates["combined"] = {
            "estimated_compute_hours": compute_hours,
            "overhead_fraction_applied": overhead_fraction,
            "estimated_wall_hours_including_overhead": compute_hours / (1.0 - overhead_fraction),
            "estimated_accelerator_hours_for_all_ranks": compute_hours / (1.0 - overhead_fraction) * world_size,
            "same_estimated_wall_hours_per_training_rank": compute_hours / (1.0 - overhead_fraction),
            "meets_target_hours": compute_hours / (1.0 - overhead_fraction) <= target_hours,
        }

    return {
        "status": "estimate_only_no_training_started",
        "world_size": world_size,
        "monitor_processes": 1,
        "target_wall_hours": target_hours,
        "target_overhead_fraction": overhead_fraction,
        "required_average_global_updates_per_second_for_target": required_rate,
        "data_candidate": {
            "dataset_id": dataset["dataset_id"],
            "published_counts": counts,
            "estimated_80pct_train_partition_tokens": candidate_train_tokens,
            "training_rights_status": dataset.get("training_status", "unspecified"),
            "approved_for_training": dataset.get("training_status") == "approved",
            "warning": "Published aggregate counts only: these are not prepared rank shards. Actual document/source-group sizes and tokenized lengths will be uneven; build a manifest from rights-approved, split data before dispatch.",
        },
        "reference_workload": {
            "pretraining_updates": pretrain_updates,
            "pretraining_global_tokens": pretrain_total_tokens,
            "pretraining_global_tokens_per_update": pretrain_tokens_per_update,
            "graph_training_updates": graph_updates,
            "graph_training_global_actions": graph_total_actions,
            "graph_training_global_actions_per_update": graph_actions_per_update,
            "candidate_corpus_passes_if_all_published_tokens_were_used_for_pretraining": pretrain_total_tokens / candidate_tokens,
            "candidate_train_partition_passes_if_80pct_were_used_for_pretraining": pretrain_total_tokens / candidate_train_tokens,
            "note": "The 80% partition is illustrative only; source-group splitting will not produce an exact 80/10/10 count split. UMR rights, annotations, and conversion are unresolved, so these corpus values are not permission to use the data.",
        },
        "rank_estimates": rank_rows,
        "time_estimates": stage_estimates,
        "time_estimate_method": "estimated compute seconds = optimizer updates / measured global synchronized updates per second; wall time adds overhead_fraction; synchronized ranks finish at the slowest rank, so each rank has the same wall time.",
    }


def _load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--dataset", type=Path, default=DEFAULT_DATASET)
    parser.add_argument("--target-hours", type=float, default=DEFAULT_TARGET_HOURS)
    parser.add_argument("--pretrain-updates-per-second", type=float,
                        help="measured cluster-wide synchronized optimizer updates/s")
    parser.add_argument("--graph-updates-per-second", type=float,
                        help="measured cluster-wide synchronized optimizer updates/s")
    parser.add_argument("--overhead-fraction", type=float, default=0.0,
                        help="reserved wall-time fraction for checkpointing, evaluation, and recovery")
    parser.add_argument("--world-size", type=int, default=WORLD_SIZE)
    args = parser.parse_args()
    result = estimate(
        _load(args.reference), _load(args.dataset),
        target_hours=args.target_hours,
        pretrain_updates_per_second=args.pretrain_updates_per_second,
        graph_updates_per_second=args.graph_updates_per_second,
        overhead_fraction=args.overhead_fraction,
        world_size=args.world_size,
    )
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
