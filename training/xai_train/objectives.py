"""Proposal-aligned objectives and sampling helpers."""
from __future__ import annotations

import math
import random
from collections import Counter, defaultdict
from typing import Any


def span_corruption(token_ids: list[int], mask_id: int, sentinel_ids: list[int], eos_id: int,
                    rng: random.Random, fraction: float = 0.15,
                    mean_span_length: float = 3.0) -> tuple[list[int], list[int]]:
    """Return corrupted source and T5-style sentinel target for real training text."""
    if not token_ids:
        raise ValueError("cannot corrupt an empty token sequence")
    if not 0 < fraction < 1 or mean_span_length <= 0 or not sentinel_ids:
        raise ValueError("invalid corruption configuration")
    target_count = max(1, min(len(token_ids), math.ceil(len(token_ids) * fraction)))
    spans: list[tuple[int, int]] = []
    occupied: set[int] = set()
    attempts = 0
    while len(occupied) < target_count and attempts < len(token_ids) * 20:
        attempts += 1
        start = rng.randrange(len(token_ids))
        if start in occupied:
            continue
        # Geometric length with expectation approximately mean_span_length.
        p = 1.0 / mean_span_length
        length = 1
        while rng.random() > p and length < len(token_ids):
            length += 1
        end = start
        while end < len(token_ids) and end - start < length and end not in occupied and len(occupied) < target_count:
            occupied.add(end)
            end += 1
        if end > start:
            spans.append((start, end))
    if not spans:
        spans = [(0, 1)]
    spans.sort()
    merged: list[tuple[int, int]] = []
    for start, end in spans:
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
        else:
            merged.append((start, end))
    source: list[int] = []
    target: list[int] = []
    cursor = 0
    for span_index, (start, end) in enumerate(merged):
        sentinel = sentinel_ids[span_index % len(sentinel_ids)]
        source.extend(token_ids[cursor:start])
        source.append(sentinel)
        target.append(sentinel)
        target.extend(token_ids[start:end])
        cursor = end
    source.extend(token_ids[cursor:])
    target.append(eos_id)
    return source, target


def accepted_reading_weights(records: list[dict[str, Any]]) -> list[float]:
    """Weights whose sum is 1 across languages, documents, then readings."""
    eligible = [r for r in records if r.get("use_graph_training") and r.get("accepted_readings")]
    languages = sorted({r["language"] for r in eligible})
    if not languages:
        raise ValueError("no graph-training readings")
    documents_by_language: dict[str, set[str]] = defaultdict(set)
    for record in eligible:
        documents_by_language[record["language"]].add(record["record_id"])
    result: list[float] = []
    for record in eligible:
        result.extend([
            1.0 / len(languages)
            / len(documents_by_language[record["language"]])
            / len(record["accepted_readings"])
        ] * len(record["accepted_readings"]))
    normalizer = sum(result)
    return [value / normalizer for value in result]


def learning_rate(update: int, total_updates: int, peak: float, warmup_fraction: float = 0.02) -> float:
    """Linear warmup followed by cosine decay to zero."""
    if total_updates <= 0 or not 0 <= update < total_updates:
        raise ValueError("update must be inside a positive training budget")
    warmup = max(1, int(total_updates * warmup_fraction))
    if update < warmup:
        return peak * (update + 1) / warmup
    progress = (update - warmup) / max(1, total_updates - warmup - 1)
    return peak * 0.5 * (1.0 + math.cos(math.pi * min(1.0, progress)))
