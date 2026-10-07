#!/usr/bin/env python3
"""Reusable disjoint-shard trainer for the KILT T-REx count-ranker.

The engine separates planning, one-shard worker execution, and final synthesis so
those stages can run on independent CI jobs. It deliberately does not evaluate a
model. The merge is exact for this model because its learned state is additive
SQLite occurrence counts; it is not a generic neural-weight averaging scheme.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import shutil
import sqlite3
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Dict, List, Mapping, Optional, Sequence, Tuple

import grounding_train as grounding

ENGINE_ID = "kilt-trex-additive-shard-trainer-v1"
RUN_MANIFEST_FILENAME = "trainer-manifest.json"
RUN_SCHEMA_VERSION = 1
MAX_SHARDS = 20


def _sha256_file(path: Path) -> Tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def _valid_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value)
    )


def _load_dataset_manifest(data_dir: Path) -> Tuple[Dict[str, Any], str, Dict[str, Any]]:
    manifest, manifest_sha256 = grounding._load_manifest(data_dir)
    train_info = manifest["files"].get("train")
    if not isinstance(train_info, dict):
        raise grounding.GroundingDataError("dataset manifest has no training file metadata")
    expected_filename = grounding.EXPECTED_SPLITS["train"]["filename"]
    if train_info.get("filename") != expected_filename:
        raise grounding.GroundingDataError(
            "dataset manifest training filename does not match the frozen KILT T-REx file"
        )
    for field in ("bytes", "records"):
        value = train_info.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 1:
            raise grounding.GroundingDataError(
                f"dataset manifest training {field} must be a positive integer"
            )
    if not _valid_sha256(train_info.get("sha256")):
        raise grounding.GroundingDataError("dataset manifest training SHA-256 is missing or invalid")
    return manifest, manifest_sha256, train_info


def _expected_shard_ranges(record_count: int, shard_count: int) -> List[Tuple[int, int]]:
    if not isinstance(shard_count, int) or isinstance(shard_count, bool):
        raise grounding.GroundingDataError("shard count must be an integer")
    if shard_count < 1 or shard_count > MAX_SHARDS:
        raise grounding.GroundingDataError(f"shard count must be between 1 and {MAX_SHARDS}")
    if record_count < shard_count:
        raise grounding.GroundingDataError(
            f"cannot create {shard_count} non-empty shards from {record_count} records"
        )
    base, remainder = divmod(record_count, shard_count)
    ranges: List[Tuple[int, int]] = []
    start = 0
    for index in range(shard_count):
        count = base + (1 if index < remainder else 0)
        end = start + count
        ranges.append((start, end))
        start = end
    return ranges


def create_plan(data_dir: Path, run_dir: Path, shard_count: int = MAX_SHARDS) -> Dict[str, Any]:
    """Validate the frozen train split and write balanced, contiguous JSONL shards."""
    _, dataset_manifest_sha256, train_info = _load_dataset_manifest(data_dir)
    train_path = data_dir / train_info["filename"]
    if not train_path.is_file():
        raise grounding.GroundingDataError(f"missing training file: {train_path}")
    ranges = _expected_shard_ranges(train_info["records"], shard_count)
    run_dir = run_dir.resolve()
    if run_dir.exists():
        raise grounding.GroundingDataError(
            f"run directory already exists: {run_dir}; choose a new path"
        )
    run_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary_dir = Path(
        tempfile.mkdtemp(prefix=f".{run_dir.name}.partial-", dir=str(run_dir.parent))
    )
    shard_dir = temporary_dir / "shards"
    shard_dir.mkdir()
    shard_digests = [hashlib.sha256() for _ in ranges]
    shard_bytes = [0 for _ in ranges]
    shard_records = [0 for _ in ranges]
    source_digest = hashlib.sha256()
    rows = 0
    current_shard = 0
    try:
        with contextlib.ExitStack() as stack:
            shard_streams = [
                stack.enter_context((shard_dir / f"shard-{index:04d}.jsonl").open("wb"))
                for index in range(shard_count)
            ]
            with train_path.open("rb") as source:
                for line_number, raw in enumerate(source, 1):
                    if line_number > train_info["records"]:
                        raise grounding.GroundingDataError(
                            f"training data has more than the declared "
                            f"{train_info['records']:,} records"
                        )
                    if not raw.strip():
                        raise grounding.GroundingDataError(
                            f"{train_path}:{line_number}: blank JSONL line"
                        )
                    row = grounding._decode_record(raw, train_path, line_number)
                    grounding.validate_record(row, "train", line_number, train_path)
                    source_digest.update(raw)
                    while line_number - 1 >= ranges[current_shard][1]:
                        current_shard += 1
                    shard_streams[current_shard].write(raw)
                    shard_digests[current_shard].update(raw)
                    shard_bytes[current_shard] += len(raw)
                    shard_records[current_shard] += 1
                    rows += 1

        source_sha256 = source_digest.hexdigest()
        actual_bytes = train_path.stat().st_size
        if actual_bytes != train_info["bytes"]:
            raise grounding.GroundingDataError(
                f"training data has {actual_bytes:,} bytes; dataset manifest declares "
                f"{train_info['bytes']:,}"
            )
        if rows != train_info["records"]:
            raise grounding.GroundingDataError(
                f"training data has {rows:,} records; dataset manifest declares "
                f"{train_info['records']:,}"
            )
        if source_sha256 != train_info["sha256"]:
            raise grounding.GroundingDataError(
                f"training data SHA-256 mismatch: got {source_sha256}, "
                f"expected {train_info['sha256']}"
            )

        shard_entries: List[Dict[str, Any]] = []
        for index, (start, end) in enumerate(ranges):
            expected_records = end - start
            if shard_records[index] != expected_records:
                raise grounding.GroundingDataError(
                    f"internal partition error for shard {index}: "
                    f"wrote {shard_records[index]} records, expected {expected_records}"
                )
            shard_entries.append(
                {
                    "shard_index": index,
                    "filename": f"shard-{index:04d}.jsonl",
                    "start_record_index": start,
                    "end_record_index_exclusive": end,
                    "records": shard_records[index],
                    "bytes": shard_bytes[index],
                    "sha256": shard_digests[index].hexdigest(),
                }
            )

        run_manifest: Dict[str, Any] = {
            "run_schema_version": RUN_SCHEMA_VERSION,
            "engine_id": ENGINE_ID,
            "dataset_id": grounding.DATASET_ID,
            "dataset_manifest_sha256": dataset_manifest_sha256,
            "created_at_utc": datetime.now(timezone.utc).isoformat(),
            "partition_strategy": "balanced-contiguous-record-ranges-v1",
            "source": {
                "filename": train_info["filename"],
                "bytes": actual_bytes,
                "records": rows,
                "sha256": source_sha256,
            },
            "shard_count": shard_count,
            "shards": shard_entries,
            "merge_contract": (
                "Sum disjoint occurrence-count sufficient statistics in ascending source-range "
                "order; for this model this is equivalent to a single full-data pass."
            ),
            "individual_shard_evaluation": "not performed by this engine",
        }
        grounding._atomic_write_json(temporary_dir / RUN_MANIFEST_FILENAME, run_manifest)
        os.replace(temporary_dir, run_dir)
        print(
            f"Planned {rows:,} records into {shard_count} disjoint shards at {run_dir}"
        )
        return run_manifest
    except BaseException:
        shutil.rmtree(temporary_dir, ignore_errors=True)
        raise


def _load_run_manifest(run_dir: Path) -> Tuple[Dict[str, Any], str]:
    manifest_path = run_dir / RUN_MANIFEST_FILENAME
    if not manifest_path.is_file():
        raise grounding.GroundingDataError(f"missing trainer run manifest: {manifest_path}")
    raw = manifest_path.read_bytes()
    try:
        manifest = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise grounding.GroundingDataError(f"invalid trainer run manifest: {exc}") from exc
    if not isinstance(manifest, dict):
        raise grounding.GroundingDataError("trainer run manifest must be a JSON object")
    if manifest.get("run_schema_version") != RUN_SCHEMA_VERSION:
        raise grounding.GroundingDataError("unsupported trainer run manifest schema")
    if manifest.get("engine_id") != ENGINE_ID or manifest.get("dataset_id") != grounding.DATASET_ID:
        raise grounding.GroundingDataError("trainer run manifest engine or dataset ID mismatch")
    source = manifest.get("source")
    shard_count = manifest.get("shard_count")
    shards = manifest.get("shards")
    if not isinstance(source, dict) or not isinstance(shards, list):
        raise grounding.GroundingDataError("trainer run manifest is missing source or shard metadata")
    source_records = source.get("records")
    if not isinstance(source_records, int) or isinstance(source_records, bool) or source_records < 1:
        raise grounding.GroundingDataError("trainer run source record count is invalid")
    if not _valid_sha256(source.get("sha256")):
        raise grounding.GroundingDataError("trainer run source SHA-256 is invalid")
    if not _valid_sha256(manifest.get("dataset_manifest_sha256")):
        raise grounding.GroundingDataError("trainer run dataset-manifest SHA-256 is invalid")
    ranges = _expected_shard_ranges(source_records, shard_count)
    if len(shards) != shard_count:
        raise grounding.GroundingDataError("trainer run shard list length does not match shard count")
    for index, (entry, expected_range) in enumerate(zip(shards, ranges)):
        start, end = expected_range
        if not isinstance(entry, dict) or entry.get("shard_index") != index:
            raise grounding.GroundingDataError("trainer run shards must be listed exactly once in order")
        if entry.get("filename") != f"shard-{index:04d}.jsonl":
            raise grounding.GroundingDataError(f"unexpected filename for shard {index}")
        if entry.get("start_record_index") != start or entry.get("end_record_index_exclusive") != end:
            raise grounding.GroundingDataError(f"shard {index} record range is not the declared partition")
        entry_records = entry.get("records")
        if (
            not isinstance(entry_records, int)
            or isinstance(entry_records, bool)
            or entry_records != end - start
        ):
            raise grounding.GroundingDataError(f"shard {index} record count does not match its range")
        if (
            not isinstance(entry.get("bytes"), int)
            or isinstance(entry["bytes"], bool)
            or entry["bytes"] < 1
        ):
            raise grounding.GroundingDataError(f"shard {index} byte count is invalid")
        if not _valid_sha256(entry.get("sha256")):
            raise grounding.GroundingDataError(f"shard {index} SHA-256 is invalid")
    if source.get("filename") != grounding.EXPECTED_SPLITS["train"]["filename"]:
        raise grounding.GroundingDataError("trainer run source filename does not match KILT T-REx")
    if (
        not isinstance(source.get("bytes"), int)
        or isinstance(source["bytes"], bool)
        or source["bytes"] < 1
    ):
        raise grounding.GroundingDataError("trainer run source byte count is invalid")
    if sum(entry["bytes"] for entry in shards) != source["bytes"]:
        raise grounding.GroundingDataError("shard byte counts do not cover the declared source file")
    return manifest, hashlib.sha256(raw).hexdigest()


def train_shard(
    run_dir: Path,
    shard_index: int,
    model_path: Optional[Path] = None,
    shard_path: Optional[Path] = None,
) -> Dict[str, Any]:
    """Train one planned shard and annotate the result with its exact lineage."""
    manifest, run_manifest_sha256 = _load_run_manifest(run_dir)
    if not isinstance(shard_index, int) or isinstance(shard_index, bool):
        raise grounding.GroundingDataError("shard index must be an integer")
    if shard_index < 0 or shard_index >= manifest["shard_count"]:
        raise grounding.GroundingDataError(
            f"shard index {shard_index} is outside 0..{manifest['shard_count'] - 1}"
        )
    shard = manifest["shards"][shard_index]
    shard_path = shard_path or (run_dir / "shards" / shard["filename"])
    target_model = model_path or (run_dir / "models" / f"shard-{shard_index:04d}.sqlite")
    trainer_metadata = {
        "run_manifest_sha256": run_manifest_sha256,
        "shard_index": shard_index,
        "shard_count": manifest["shard_count"],
        "start_record_index": shard["start_record_index"],
        "end_record_index_exclusive": shard["end_record_index_exclusive"],
        "records": shard["records"],
        "bytes": shard["bytes"],
        "sha256": shard["sha256"],
    }
    metadata = grounding.train_index(
        shard_path,
        target_model,
        expected_sha256=shard["sha256"],
        expected_bytes=shard["bytes"],
        expected_records=shard["records"],
        dataset_manifest_sha256=manifest["dataset_manifest_sha256"],
        trainer_metadata=trainer_metadata,
    )
    print(
        f"Completed shard {shard_index + 1}/{manifest['shard_count']}: "
        f"{metadata['training_records']:,} records -> {target_model}"
    )
    return metadata


def _read_model_metadata(conn: sqlite3.Connection, path: Path) -> Dict[str, Any]:
    try:
        rows = conn.execute("SELECT key, value FROM metadata").fetchall()
    except sqlite3.Error as exc:
        raise grounding.GroundingDataError(f"{path} has no readable model metadata: {exc}") from exc
    try:
        metadata = {key: json.loads(value) for key, value in rows}
    except (TypeError, json.JSONDecodeError) as exc:
        raise grounding.GroundingDataError(f"{path} contains invalid model metadata: {exc}") from exc
    if metadata.get("model_schema_version") != grounding.MODEL_SCHEMA_VERSION:
        raise grounding.GroundingDataError(f"{path} has an unsupported model schema")
    return metadata


def _validate_shard_model(
    path: Path, run_manifest: Mapping[str, Any], run_manifest_sha256: str, shard: Mapping[str, Any]
) -> Tuple[sqlite3.Connection, Dict[str, Any], str, int]:
    if not path.is_file():
        raise grounding.GroundingDataError(f"missing model for shard {shard['shard_index']}: {path}")
    uri = path.resolve().as_uri() + "?mode=ro"
    try:
        conn = sqlite3.connect(uri, uri=True)
        metadata = _read_model_metadata(conn, path)
        shard_metadata = metadata.get("trainer_shard")
        expected_shard_metadata = {
            "run_manifest_sha256": run_manifest_sha256,
            "shard_index": shard["shard_index"],
            "shard_count": run_manifest["shard_count"],
            "start_record_index": shard["start_record_index"],
            "end_record_index_exclusive": shard["end_record_index_exclusive"],
            "records": shard["records"],
            "bytes": shard["bytes"],
            "sha256": shard["sha256"],
        }
        if shard_metadata != expected_shard_metadata:
            raise grounding.GroundingDataError(
                f"{path} is not the model for planned shard {shard['shard_index']}"
            )
        if metadata.get("dataset_id") != grounding.DATASET_ID:
            raise grounding.GroundingDataError(f"{path} has a different dataset ID")
        if metadata.get("model_id") != "kilt-trex-exact-surface-count-ranker-v1":
            raise grounding.GroundingDataError(f"{path} has a different model ID")
        if metadata.get("dataset_manifest_sha256") != run_manifest["dataset_manifest_sha256"]:
            raise grounding.GroundingDataError(f"{path} was built from a different dataset manifest")
        if metadata.get("training_sha256") != shard["sha256"]:
            raise grounding.GroundingDataError(f"{path} has a different shard data hash")
        if metadata.get("training_bytes") != shard["bytes"]:
            raise grounding.GroundingDataError(f"{path} has a different shard byte count")
        if metadata.get("training_records") != shard["records"]:
            raise grounding.GroundingDataError(f"{path} has a different shard record count")
        candidate_rows = conn.execute("SELECT COUNT(*) FROM candidates").fetchone()[0]
        indexed_pairs = conn.execute("SELECT COALESCE(SUM(count), 0) FROM candidates").fetchone()[0]
        distinct_keys = conn.execute(
            "SELECT COUNT(*) FROM (SELECT subject_key, relation_key FROM candidates "
            "GROUP BY subject_key, relation_key)"
        ).fetchone()[0]
        if candidate_rows != metadata.get("candidate_rows"):
            raise grounding.GroundingDataError(f"{path} candidate-row metadata does not match its index")
        if indexed_pairs != metadata.get("indexed_subject_relation_answer_pairs"):
            raise grounding.GroundingDataError(f"{path} count metadata does not match its index")
        if distinct_keys != metadata.get("distinct_subject_relation_keys"):
            raise grounding.GroundingDataError(f"{path} query-key metadata does not match its index")
        model_sha256, model_bytes = _sha256_file(path)
        return conn, metadata, model_sha256, model_bytes
    except BaseException:
        try:
            conn.close()
        except (UnboundLocalError, sqlite3.Error):
            pass
        raise


def _write_model_metadata(conn: sqlite3.Connection, metadata: Mapping[str, Any]) -> None:
    conn.executemany(
        "INSERT INTO metadata(key, value) VALUES (?, ?)",
        [
            (key, json.dumps(value, ensure_ascii=False, sort_keys=True))
            for key, value in metadata.items()
        ],
    )


def merge_shards(
    run_dir: Path,
    output_model: Optional[Path] = None,
    shard_models: Optional[Mapping[int, Path]] = None,
    shard_model_loader: Optional[Callable[[int, Mapping[str, Any]], Path]] = None,
    shard_model_cleanup: Optional[Callable[[int, Path], None]] = None,
) -> Dict[str, Any]:
    """Synthesize all worker indexes by summing additive counts, without evaluation."""
    run_manifest, run_manifest_sha256 = _load_run_manifest(run_dir)
    if shard_model_loader is not None and shard_models:
        raise grounding.GroundingDataError(
            "provide either shard model paths or a one-at-a-time loader, not both"
        )
    model_paths = (
        {}
        if shard_model_loader is not None
        else {
            index: (shard_models or {}).get(
                index, run_dir / "models" / f"shard-{index:04d}.sqlite"
            )
            for index in range(run_manifest["shard_count"])
        }
    )
    extra_indices = set(shard_models or {}) - set(range(run_manifest["shard_count"]))
    if extra_indices:
        raise grounding.GroundingDataError(f"unexpected shard-model indices: {sorted(extra_indices)}")
    resolved_inputs = [path.resolve() for path in model_paths.values()]
    if len(set(resolved_inputs)) != len(resolved_inputs):
        raise grounding.GroundingDataError("each shard must have a distinct model file")

    source = run_manifest["source"]
    target_model = output_model or (run_dir / "merged-model.sqlite")
    target_model = target_model.resolve()
    if target_model in resolved_inputs:
        raise grounding.GroundingDataError("merged model path must not overwrite a shard model")
    if target_model.exists():
        raise grounding.GroundingDataError(
            f"merged model already exists: {target_model}; choose a new path"
        )
    target_model.parent.mkdir(parents=True, exist_ok=True)
    temp_path = target_model.with_name(target_model.name + f".partial-{os.getpid()}")
    for suffix in ("", "-journal", "-wal", "-shm"):
        candidate = Path(str(temp_path) + suffix)
        if candidate.exists():
            candidate.unlink()

    target: Optional[sqlite3.Connection] = None
    aggregate_records = aggregate_labeled = aggregate_pairs = 0
    shard_model_summaries: List[Dict[str, Any]] = []
    try:
        target = sqlite3.connect(str(temp_path))
        target.execute("PRAGMA journal_mode=DELETE")
        target.execute("PRAGMA synchronous=NORMAL")
        target.execute("PRAGMA temp_store=FILE")
        target.execute(
            "CREATE TABLE candidates ("
            "subject_key TEXT NOT NULL, relation_key TEXT NOT NULL, "
            "answer_key TEXT NOT NULL, answer_surface TEXT NOT NULL, count INTEGER NOT NULL, "
            "PRIMARY KEY (subject_key, relation_key, answer_key)) WITHOUT ROWID"
        )
        target.execute(
            "CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID"
        )
        target.execute("BEGIN")
        upsert = (
            "INSERT INTO candidates(subject_key, relation_key, answer_key, answer_surface, count) "
            "VALUES (?, ?, ?, ?, ?) ON CONFLICT(subject_key, relation_key, answer_key) "
            "DO UPDATE SET count=count+excluded.count"
        )
        first_ranking_note: Optional[str] = None
        for index in range(run_manifest["shard_count"]):
            shard = run_manifest["shards"][index]
            shard_path: Optional[Path] = None
            source_conn: Optional[sqlite3.Connection] = None
            try:
                shard_path = (
                    shard_model_loader(index, shard)
                    if shard_model_loader is not None
                    else model_paths[index]
                )
                if shard_path.resolve() == target_model:
                    raise grounding.GroundingDataError(
                        "merged model path must not overwrite a shard model"
                    )
                source_conn, metadata, model_sha256, model_bytes = _validate_shard_model(
                    shard_path, run_manifest, run_manifest_sha256, shard
                )
                # Stable source-range order preserves the serial first-surface tie behavior.
                target.executemany(
                    upsert,
                    source_conn.execute(
                        "SELECT subject_key, relation_key, answer_key, answer_surface, count "
                        "FROM candidates ORDER BY subject_key, relation_key, answer_key"
                    ),
                )
                aggregate_records += metadata["training_records"]
                aggregate_labeled += metadata["labeled_training_records"]
                aggregate_pairs += metadata["indexed_subject_relation_answer_pairs"]
                if first_ranking_note is None:
                    first_ranking_note = metadata.get("ranking_note")
                shard_model_summaries.append(
                    {
                        "shard_index": index,
                        "model_sha256": model_sha256,
                        "model_bytes": model_bytes,
                    }
                )
            finally:
                if source_conn is not None:
                    source_conn.close()
                if shard_model_cleanup is not None and shard_path is not None:
                    shard_model_cleanup(index, shard_path)

        if aggregate_records != source["records"]:
            raise grounding.GroundingDataError(
                f"merged {aggregate_records:,} records; expected {source['records']:,}"
            )
        candidate_rows = target.execute("SELECT COUNT(*) FROM candidates").fetchone()[0]
        distinct_keys = target.execute(
            "SELECT COUNT(*) FROM (SELECT subject_key, relation_key FROM candidates "
            "GROUP BY subject_key, relation_key)"
        ).fetchone()[0]
        merged_metadata: Dict[str, Any] = {
            "model_schema_version": grounding.MODEL_SCHEMA_VERSION,
            "model_id": "kilt-trex-exact-surface-count-ranker-v1",
            "dataset_id": grounding.DATASET_ID,
            "created_at_utc": datetime.now(timezone.utc).isoformat(),
            "training_filename": source["filename"],
            "training_bytes": source["bytes"],
            "training_sha256": source["sha256"],
            "dataset_manifest_sha256": run_manifest["dataset_manifest_sha256"],
            "training_records": aggregate_records,
            "labeled_training_records": aggregate_labeled,
            "indexed_subject_relation_answer_pairs": aggregate_pairs,
            "candidate_rows": candidate_rows,
            "distinct_subject_relation_keys": distinct_keys,
            "ranking_note": first_ranking_note
            or (
                "Raw occurrence counts rank exact normalized subject-alias/relation matches. "
                "They are not calibrated probabilities."
            ),
            "trainer_merge": {
                "run_manifest_sha256": run_manifest_sha256,
                "shard_count": run_manifest["shard_count"],
                "merge_strategy": "sum-disjoint-count-sufficient-statistics-in-source-order-v1",
                "individual_shards_evaluated": False,
                "worker_models": shard_model_summaries,
            },
        }
        _write_model_metadata(target, merged_metadata)
        target.commit()
        target.close()
        target = None
        os.replace(temp_path, target_model)
        output_sha256, output_bytes = _sha256_file(target_model)
        result = {
            "model_path": str(target_model),
            "model_sha256": output_sha256,
            "model_bytes": output_bytes,
            "training_records": aggregate_records,
            "labeled_training_records": aggregate_labeled,
            "candidate_rows": candidate_rows,
            "distinct_subject_relation_keys": distinct_keys,
            "shard_count": run_manifest["shard_count"],
            "individual_shards_evaluated": False,
        }
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
        return result
    except BaseException:
        if target is not None:
            target.close()
        for suffix in ("", "-journal", "-wal", "-shm"):
            candidate = Path(str(temp_path) + suffix)
            try:
                candidate.unlink()
            except FileNotFoundError:
                pass
        raise


def _parse_shard_model(value: str) -> Tuple[int, Path]:
    index_text, separator, path_text = value.partition("=")
    if not separator or not path_text:
        raise argparse.ArgumentTypeError("expected INDEX=PATH")
    try:
        index = int(index_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("shard index in INDEX=PATH must be an integer") from exc
    if index < 0:
        raise argparse.ArgumentTypeError("shard index must be non-negative")
    return index, Path(path_text)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Plan disjoint KILT T-REx shards, train one additive count-index per worker, "
            "and merge all shards. No evaluation is performed."
        )
    )
    commands = parser.add_subparsers(dest="command", required=True)

    plan = commands.add_parser("plan", help="validate the training split and create balanced shards")
    plan.add_argument("--data-dir", default="data/kilt/trex")
    plan.add_argument("--run-dir", default="data/kilt/trex/trainer-run")
    plan.add_argument("--shards", type=int, default=MAX_SHARDS, help=f"worker count (1-{MAX_SHARDS})")
    plan.set_defaults(func=lambda args: create_plan(Path(args.data_dir), Path(args.run_dir), args.shards))

    worker = commands.add_parser("train-shard", help="train exactly one planned shard")
    worker.add_argument("--run-dir", default="data/kilt/trex/trainer-run")
    worker.add_argument("--shard-index", type=int, required=True, help="zero-based shard index")
    worker.add_argument("--model", help="optional output path; defaults under RUN_DIR/models")
    worker.set_defaults(
        func=lambda args: train_shard(
            Path(args.run_dir), args.shard_index, Path(args.model) if args.model else None
        )
    )

    merge = commands.add_parser("merge", help="merge every worker model; does not evaluate")
    merge.add_argument("--run-dir", default="data/kilt/trex/trainer-run")
    merge.add_argument("--output-model", help="optional merged model path")
    merge.add_argument(
        "--shard-model",
        action="append",
        type=_parse_shard_model,
        default=[],
        metavar="INDEX=PATH",
        help="override a worker model path (repeat for models collected from remote jobs)",
    )
    merge.set_defaults(func=_cmd_merge)
    return parser


def _cmd_merge(args: argparse.Namespace) -> int:
    shard_models: Dict[int, Path] = {}
    for index, path in args.shard_model:
        if index in shard_models:
            raise grounding.GroundingDataError(f"shard model {index} was specified more than once")
        shard_models[index] = path
    merge_shards(
        Path(args.run_dir),
        Path(args.output_model) if args.output_model else None,
        shard_models,
    )
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        result = args.func(args)
        return 0 if result is not None else 1
    except (grounding.GroundingDataError, OSError, sqlite3.Error, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
