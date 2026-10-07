#!/usr/bin/env python3
"""Prepare and train a small, auditable KILT T-REx slot-filling baseline.

This is an offline research utility, not a production natural-language parser.
It uses only Python's standard library. Importing this module never downloads data
or trains a model; those actions require explicit subcommands.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sqlite3
import sys
import tempfile
import unicodedata
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Iterator, List, Mapping, Optional, Sequence, Tuple

DATASET_ID = "kilt-trex-slot-filling-v1"
KILT_REPOSITORY = "https://github.com/facebookresearch/KILT"
KILT_DATA_CATALOGUE = "https://github.com/facebookresearch/KILT#kilt-data-catalogue"
MODEL_SCHEMA_VERSION = 1
EXPECTED_SPLITS: Dict[str, Dict[str, Any]] = {
    "train": {
        "filename": "trex-train-kilt.jsonl",
        "url": "https://dl.fbaipublicfiles.com/KILT/trex-train-kilt.jsonl",
        "records": 2_284_168,
        "bytes": 1_752_330_104,
        "labeled": True,
    },
    "dev": {
        "filename": "trex-dev-kilt.jsonl",
        "url": "https://dl.fbaipublicfiles.com/KILT/trex-dev-kilt.jsonl",
        "records": 5_000,
        "bytes": 3_803_558,
        "labeled": True,
    },
    "test": {
        "filename": "trex-test_without_answers-kilt.jsonl",
        "url": "https://dl.fbaipublicfiles.com/KILT/trex-test_without_answers-kilt.jsonl",
        "records": 5_000,
        "bytes": 895_854,
        "labeled": False,
    },
}
MANIFEST_FILENAME = "dataset-manifest.json"
PROMPT_SEPARATOR = "[SEP]"


class GroundingDataError(ValueError):
    """Raised when a KILT file or record violates the declared data contract."""


def normalize_text(value: str) -> str:
    """Normalize surfaces conservatively for exact lexical lookup."""
    return " ".join(unicodedata.normalize("NFKC", value).casefold().split())


def parse_prompt(prompt: str) -> Tuple[str, str]:
    """Parse KILT T-REx's `subject [SEP] relation` slot-filling input."""
    parts = prompt.split(PROMPT_SEPARATOR)
    if len(parts) != 2:
        raise GroundingDataError(
            f"expected one {PROMPT_SEPARATOR!r} separator in T-REx input"
        )
    subject, relation = (part.strip() for part in parts)
    if not subject or not relation:
        raise GroundingDataError("T-REx subject and relation must both be non-empty")
    return subject, relation


def _clean_surfaces(values: Any) -> List[str]:
    if not isinstance(values, list):
        return []
    result: List[str] = []
    seen = set()
    for value in values:
        if not isinstance(value, str) or not value.strip():
            continue
        key = normalize_text(value)
        if key and key not in seen:
            seen.add(key)
            result.append(value.strip())
    return result


def subject_surfaces(row: Mapping[str, Any]) -> List[str]:
    """Return the query subject and declared KILT subject aliases."""
    subject, _ = parse_prompt(row["input"])
    meta = row.get("meta")
    if not isinstance(meta, dict):
        meta = {}
    candidates = [subject]
    candidates.extend(_clean_surfaces(meta.get("sub_surface")))
    candidates.extend(_clean_surfaces(meta.get("subj_aliases")))
    result: List[str] = []
    seen = set()
    for surface in candidates:
        key = normalize_text(surface)
        if key and key not in seen:
            seen.add(key)
            result.append(surface.strip())
    return result


def answer_surfaces(row: Mapping[str, Any]) -> List[str]:
    """Read only labeled output answers; never infer labels from metadata."""
    outputs = row.get("output")
    if not isinstance(outputs, list):
        raise GroundingDataError("record output must be a list")
    candidates = []
    for output in outputs:
        if isinstance(output, dict) and isinstance(output.get("answer"), str):
            candidates.append(output["answer"])
    return _clean_surfaces(candidates)


def validate_record(row: Any, split: str, line_number: int, path: Path) -> bool:
    """Validate a KILT row and return whether it contains answer labels."""
    where = f"{path}:{line_number}"
    if not isinstance(row, dict):
        raise GroundingDataError(f"{where}: each JSONL record must be an object")
    if not isinstance(row.get("id"), str) or not row["id"].strip():
        raise GroundingDataError(f"{where}: id must be a non-empty string")
    if not isinstance(row.get("input"), str):
        raise GroundingDataError(f"{where}: input must be a string")
    try:
        parse_prompt(row["input"])
    except GroundingDataError as exc:
        raise GroundingDataError(f"{where}: {exc}") from exc
    if not isinstance(row.get("meta"), dict):
        raise GroundingDataError(f"{where}: meta must be an object")
    if split == "test" and "output" not in row:
        return False
    if not isinstance(row.get("output"), list):
        raise GroundingDataError(f"{where}: output must be a list")
    labeled = bool(answer_surfaces(row))
    if split == "test" and labeled:
        raise GroundingDataError(
            f"{where}: answer-withheld test split unexpectedly contains answer labels"
        )
    return labeled


def _decode_record(raw: bytes, path: Path, line_number: int) -> Any:
    try:
        return json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GroundingDataError(f"{path}:{line_number}: invalid UTF-8 JSONL record: {exc}") from exc


def _iter_records(
    path: Path, digest: Optional[Any] = None
) -> Iterator[Tuple[int, Dict[str, Any]]]:
    with path.open("rb") as stream:
        for line_number, raw in enumerate(stream, 1):
            if digest is not None:
                digest.update(raw)
            if not raw.strip():
                raise GroundingDataError(f"{path}:{line_number}: blank JSONL line")
            row = _decode_record(raw, path, line_number)
            if not isinstance(row, dict):
                raise GroundingDataError(f"{path}:{line_number}: record must be an object")
            yield line_number, row


def _sha256_bytes(path: Path) -> Tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def _atomic_write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def download_split(data_dir: Path, split: str, timeout: int = 60, force: bool = False) -> Path:
    spec = EXPECTED_SPLITS[split]
    data_dir.mkdir(parents=True, exist_ok=True)
    destination = data_dir / spec["filename"]
    if destination.exists():
        existing_size = destination.stat().st_size
        if existing_size == spec["bytes"] and not force:
            print(f"Keeping existing {split}: {destination} ({existing_size:,} bytes)")
            return destination
        if not force:
            raise GroundingDataError(
                f"{destination} exists with {existing_size:,} bytes; expected "
                f"{spec['bytes']:,}. Use --force to replace it."
            )

    temporary = destination.with_name(destination.name + ".part")
    if temporary.exists():
        temporary.unlink()
    request = urllib.request.Request(
        spec["url"], headers={"User-Agent": "xai-grounding-preparation/1.0"}
    )
    written = 0
    last_report = 0
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response, temporary.open("wb") as stream:
            length_header = response.headers.get("Content-Length")
            if length_header is not None and int(length_header) != spec["bytes"]:
                raise GroundingDataError(
                    f"server Content-Length for {split} is {length_header}; "
                    f"expected {spec['bytes']} frozen-release bytes"
                )
            while True:
                block = response.read(1024 * 1024)
                if not block:
                    break
                stream.write(block)
                written += len(block)
                if written - last_report >= 128 * 1024 * 1024:
                    print(f"{split}: downloaded {written:,}/{spec['bytes']:,} bytes", flush=True)
                    last_report = written
            stream.flush()
            os.fsync(stream.fileno())
        if written != spec["bytes"]:
            raise GroundingDataError(
                f"downloaded {written:,} bytes for {split}; expected {spec['bytes']:,}"
            )
        os.replace(temporary, destination)
    except BaseException:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise
    print(f"Downloaded {split}: {destination} ({written:,} bytes)")
    return destination


def download_splits(data_dir: Path, timeout: int = 60, force: bool = False) -> None:
    for split in ("train", "dev", "test"):
        download_split(data_dir, split, timeout=timeout, force=force)


def preflight_split(path: Path, split: str, check_release_size: bool = True) -> Dict[str, Any]:
    if split not in EXPECTED_SPLITS:
        raise GroundingDataError(f"unknown split: {split}")
    spec = EXPECTED_SPLITS[split]
    if not path.is_file():
        raise GroundingDataError(f"missing {split} data file: {path}")
    if check_release_size and path.stat().st_size != spec["bytes"]:
        raise GroundingDataError(
            f"{path} has {path.stat().st_size:,} bytes; expected {spec['bytes']:,}"
        )
    digest = hashlib.sha256()
    rows = labeled_rows = empty_output_rows = 0
    for line_number, row in _iter_records(path, digest=digest):
        labeled = validate_record(row, split, line_number, path)
        rows += 1
        labeled_rows += int(labeled)
        empty_output_rows += int(not row.get("output", []))
    if check_release_size and rows != spec["records"]:
        raise GroundingDataError(
            f"{path} has {rows:,} records; expected {spec['records']:,}"
        )
    if split in ("train", "dev") and labeled_rows == 0:
        raise GroundingDataError(f"{path} contains no answer labels in {split} split")
    if split == "test" and labeled_rows != 0:
        raise GroundingDataError(f"{path} is not answer-withheld as expected")
    return {
        "filename": path.name,
        "source_url": spec["url"],
        "bytes": path.stat().st_size,
        "sha256": digest.hexdigest(),
        "records": rows,
        "labeled_records": labeled_rows,
        "empty_output_records": empty_output_rows,
    }


def prepare_manifest(data_dir: Path, manifest_path: Optional[Path] = None) -> Dict[str, Any]:
    splits: Dict[str, Any] = {}
    for split in ("train", "dev", "test"):
        path = data_dir / EXPECTED_SPLITS[split]["filename"]
        summary = preflight_split(path, split)
        expected = EXPECTED_SPLITS[split]
        summary["expected_records"] = expected["records"]
        summary["expected_bytes"] = expected["bytes"]
        splits[split] = summary
        print(
            f"Validated {split}: {summary['records']:,} records, "
            f"{summary['labeled_records']:,} labeled, SHA-256 {summary['sha256']}"
        )

    manifest: Dict[str, Any] = {
        "dataset_id": DATASET_ID,
        "dataset_name": "KILT T-REx (KILT JSONL), English slot filling",
        "source_repository": KILT_REPOSITORY,
        "source_catalogue": KILT_DATA_CATALOGUE,
        "prepared_at_utc": datetime.now(timezone.utc).isoformat(),
        "files": splits,
        "task_scope": {
            "input": "subject surface [SEP] relation label",
            "target": "one or more answer surface forms",
            "not_provided_by_this_release": [
                "sentence-level mention spans or token labels",
                "stable Wikidata QIDs for subject, predicate, or object",
                "answer labels for the test split",
            ],
            "claim_boundary": (
                "Supports a relation-aware surface-form slot-filling baseline only; "
                "it does not train a general sentence extractor or establish factual truth."
            ),
        },
        "licensing_note": (
            "KILT repository code is MIT-licensed; that does not by itself grant rights "
            "to all underlying dataset or Wikipedia content. Review the source dataset "
            "and Wikipedia terms before redistribution or deployment."
        ),
    }
    target = manifest_path or (data_dir / MANIFEST_FILENAME)
    _atomic_write_json(target, manifest)
    print(f"Wrote data manifest: {target}")
    return manifest


def _load_manifest(data_dir: Path) -> Tuple[Dict[str, Any], str]:
    path = data_dir / MANIFEST_FILENAME
    if not path.is_file():
        raise GroundingDataError(
            f"missing {path}; run the `prepare` or `preflight` subcommand first"
        )
    raw = path.read_bytes()
    try:
        manifest = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise GroundingDataError(f"invalid dataset manifest {path}: {exc}") from exc
    if not isinstance(manifest, dict) or manifest.get("dataset_id") != DATASET_ID:
        raise GroundingDataError(f"{path} is not a {DATASET_ID} data manifest")
    if not isinstance(manifest.get("files"), dict) or "train" not in manifest["files"]:
        raise GroundingDataError(f"{path} has no training split record")
    return manifest, hashlib.sha256(raw).hexdigest()


def train_index(
    train_path: Path,
    model_path: Path,
    expected_sha256: Optional[str] = None,
    expected_bytes: Optional[int] = None,
    expected_records: Optional[int] = None,
    dataset_manifest_sha256: Optional[str] = None,
    force: bool = False,
) -> Dict[str, Any]:
    """Build a SQLite count index. This function is only called by `train`."""
    if not train_path.is_file():
        raise GroundingDataError(f"training file does not exist: {train_path}")
    if expected_bytes is not None and train_path.stat().st_size != expected_bytes:
        raise GroundingDataError(
            f"training data size changed: got {train_path.stat().st_size}, expected {expected_bytes}"
        )
    if model_path.exists() and not force:
        raise GroundingDataError(f"model already exists: {model_path}; use --force to replace it")
    model_path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = model_path.with_name(model_path.name + f".partial-{os.getpid()}")
    for suffix in ("", "-journal", "-wal", "-shm"):
        candidate = Path(str(temp_path) + suffix)
        if candidate.exists():
            candidate.unlink()

    digest = hashlib.sha256()
    conn: Optional[sqlite3.Connection] = None
    rows = labeled_rows = indexed_pairs = 0
    try:
        conn = sqlite3.connect(str(temp_path))
        conn.execute("PRAGMA journal_mode=DELETE")
        conn.execute("PRAGMA synchronous=NORMAL")
        conn.execute("PRAGMA temp_store=FILE")
        conn.execute(
            "CREATE TABLE candidates ("
            "subject_key TEXT NOT NULL, relation_key TEXT NOT NULL, "
            "answer_key TEXT NOT NULL, answer_surface TEXT NOT NULL, count INTEGER NOT NULL, "
            "PRIMARY KEY (subject_key, relation_key, answer_key)) WITHOUT ROWID"
        )
        conn.execute(
            "CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID"
        )
        conn.execute("BEGIN")
        upsert = (
            "INSERT INTO candidates(subject_key, relation_key, answer_key, answer_surface, count) "
            "VALUES (?, ?, ?, ?, 1) ON CONFLICT(subject_key, relation_key, answer_key) "
            "DO UPDATE SET count=count+1"
        )
        for line_number, row in _iter_records(train_path, digest=digest):
            validate_record(row, "train", line_number, train_path)
            rows += 1
            answers = answer_surfaces(row)
            if not answers:
                continue
            labeled_rows += 1
            _, relation = parse_prompt(row["input"])
            relation_key = normalize_text(relation)
            answer_by_key: Dict[str, str] = {}
            for answer in answers:
                answer_by_key.setdefault(normalize_text(answer), answer)
            aliases = subject_surfaces(row)
            for subject in aliases:
                subject_key = normalize_text(subject)
                for answer_key, answer_surface in answer_by_key.items():
                    conn.execute(
                        upsert,
                        (subject_key, relation_key, answer_key, answer_surface),
                    )
                    indexed_pairs += 1
            if rows % 10_000 == 0:
                conn.commit()
                conn.execute("BEGIN")
                print(f"Indexed {rows:,} / {expected_records or 'all'} training rows", flush=True)
        conn.commit()
        observed_sha256 = digest.hexdigest()
        if expected_records is not None and rows != expected_records:
            raise GroundingDataError(
                f"training file has {rows:,} records; expected {expected_records:,}"
            )
        if expected_sha256 is not None and observed_sha256 != expected_sha256:
            raise GroundingDataError(
                f"training file SHA-256 mismatch: got {observed_sha256}, expected {expected_sha256}"
            )
        if rows == 0 or labeled_rows == 0:
            raise GroundingDataError("training data has no usable labeled records")
        candidate_rows = conn.execute("SELECT COUNT(*) FROM candidates").fetchone()[0]
        query_keys = conn.execute(
            "SELECT COUNT(*) FROM (SELECT subject_key, relation_key FROM candidates "
            "GROUP BY subject_key, relation_key)"
        ).fetchone()[0]
        model_metadata = {
            "model_schema_version": MODEL_SCHEMA_VERSION,
            "model_id": "kilt-trex-exact-surface-count-ranker-v1",
            "dataset_id": DATASET_ID,
            "created_at_utc": datetime.now(timezone.utc).isoformat(),
            "training_filename": train_path.name,
            "training_bytes": train_path.stat().st_size,
            "training_sha256": observed_sha256,
            "dataset_manifest_sha256": dataset_manifest_sha256,
            "training_records": rows,
            "labeled_training_records": labeled_rows,
            "indexed_subject_relation_answer_pairs": indexed_pairs,
            "candidate_rows": candidate_rows,
            "distinct_subject_relation_keys": query_keys,
            "ranking_note": (
                "Raw occurrence counts rank exact normalized subject-alias/relation matches. "
                "They are not calibrated probabilities."
            ),
        }
        conn.executemany(
            "INSERT INTO metadata(key, value) VALUES (?, ?)",
            [(key, json.dumps(value, ensure_ascii=False, sort_keys=True))
             for key, value in model_metadata.items()],
        )
        conn.commit()
        conn.close()
        conn = None
        os.replace(temp_path, model_path)
        print(
            f"Wrote model index {model_path}: {rows:,} rows, "
            f"{candidate_rows:,} candidate entries, {query_keys:,} query keys"
        )
        return model_metadata
    except BaseException:
        if conn is not None:
            conn.close()
        for suffix in ("", "-journal", "-wal", "-shm"):
            candidate = Path(str(temp_path) + suffix)
            try:
                candidate.unlink()
            except FileNotFoundError:
                pass
        raise


def _open_model(model_path: Path) -> sqlite3.Connection:
    if not model_path.is_file():
        raise GroundingDataError(f"model index does not exist: {model_path}")
    uri = model_path.resolve().as_uri() + "?mode=ro"
    conn = sqlite3.connect(uri, uri=True)
    metadata_rows = conn.execute("SELECT key, value FROM metadata").fetchall()
    metadata = {key: json.loads(value) for key, value in metadata_rows}
    if metadata.get("model_schema_version") != MODEL_SCHEMA_VERSION:
        conn.close()
        raise GroundingDataError("unsupported or missing model schema version")
    return conn


def predict_candidates(
    conn: sqlite3.Connection, row: Mapping[str, Any], limit: int = 5
) -> List[Dict[str, Any]]:
    if limit < 1:
        raise ValueError("candidate limit must be positive")
    _, relation = parse_prompt(row["input"])
    relation_key = normalize_text(relation)
    counts: Dict[str, Tuple[str, int]] = {}
    for subject in subject_surfaces(row):
        for answer_key, answer_surface, count in conn.execute(
            "SELECT answer_key, answer_surface, count FROM candidates "
            "WHERE subject_key=? AND relation_key=?",
            (normalize_text(subject), relation_key),
        ):
            prior = counts.get(answer_key)
            if prior is None or count > prior[1]:
                counts[answer_key] = (answer_surface, count)
    ranked = sorted(
        ((key, surface, count) for key, (surface, count) in counts.items()),
        key=lambda item: (-item[2], item[0], item[1]),
    )
    total = sum(count for _, _, count in ranked)
    return [
        {
            "answer_surface": surface,
            "training_occurrences": count,
            "candidate_share": (count / total) if total else 0.0,
            "candidate_share_is_calibrated_probability": False,
        }
        for _, surface, count in ranked[:limit]
    ]


def evaluate_file(model_path: Path, data_path: Path, top_k: int = 5) -> Dict[str, Any]:
    if top_k < 1:
        raise ValueError("top_k must be positive")
    conn = _open_model(model_path)
    digest = hashlib.sha256()
    records = labeled = with_candidates = top1_hits = topk_hits = 0
    try:
        for line_number, row in _iter_records(data_path, digest=digest):
            validate_record(row, "dev", line_number, data_path)
            answers = answer_surfaces(row)
            records += 1
            if not answers:
                continue
            labeled += 1
            candidates = predict_candidates(conn, row, limit=top_k)
            if candidates:
                with_candidates += 1
            gold = {normalize_text(answer) for answer in answers}
            predicted = [normalize_text(candidate["answer_surface"]) for candidate in candidates]
            if predicted and predicted[0] in gold:
                top1_hits += 1
            if any(answer in gold for answer in predicted):
                topk_hits += 1
    finally:
        conn.close()
    if labeled == 0:
        raise GroundingDataError(f"evaluation data has no labeled records: {data_path}")
    coverage = with_candidates / labeled
    report = {
        "report_schema_version": 1,
        "model_path": str(model_path),
        "evaluation_file": str(data_path),
        "evaluation_sha256": digest.hexdigest(),
        "records": records,
        "labeled_records": labeled,
        "records_with_candidates": with_candidates,
        "coverage": coverage,
        "top1_exact_match_over_labeled": top1_hits / labeled,
        "top_k": top_k,
        "top_k_hit_rate_over_labeled": topk_hits / labeled,
        "top1_exact_match_given_coverage": (top1_hits / with_candidates) if with_candidates else None,
        "metrics_note": (
            "Exact normalized answer-surface match on labeled development records; "
            "unmatched/uncovered examples count as misses. This is not entity-ID accuracy, "
            "span extraction quality, or factual verification."
        ),
    }
    return report


def _cmd_prepare(args: argparse.Namespace) -> int:
    data_dir = Path(args.data_dir)
    download_splits(data_dir, timeout=args.timeout, force=args.force)
    prepare_manifest(data_dir)
    return 0


def _cmd_download(args: argparse.Namespace) -> int:
    download_splits(Path(args.data_dir), timeout=args.timeout, force=args.force)
    return 0


def _cmd_preflight(args: argparse.Namespace) -> int:
    prepare_manifest(Path(args.data_dir), Path(args.manifest) if args.manifest else None)
    return 0


def _cmd_train(args: argparse.Namespace) -> int:
    data_dir = Path(args.data_dir)
    manifest, manifest_sha256 = _load_manifest(data_dir)
    train_info = manifest["files"]["train"]
    if train_info.get("filename") != EXPECTED_SPLITS["train"]["filename"]:
        raise GroundingDataError("manifest training filename does not match the frozen KILT T-REx file")
    train_index(
        data_dir / EXPECTED_SPLITS["train"]["filename"],
        Path(args.model),
        expected_sha256=train_info.get("sha256"),
        expected_bytes=train_info.get("bytes"),
        expected_records=train_info.get("records"),
        dataset_manifest_sha256=manifest_sha256,
        force=args.force,
    )
    return 0


def _cmd_evaluate(args: argparse.Namespace) -> int:
    report = evaluate_file(Path(args.model), Path(args.data), top_k=args.top_k)
    serialized = json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.output:
        _atomic_write_json(Path(args.output), report)
        print(f"Wrote evaluation report: {args.output}")
    else:
        print(serialized, end="")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Prepare KILT T-REx and train/evaluate an exact surface-count slot-filling "
            "baseline. Training is only performed by the explicit `train` subcommand."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    prepare = subparsers.add_parser(
        "prepare", help="download all official splits and validate/write a SHA-256 manifest"
    )
    prepare.add_argument("--data-dir", default="data/kilt/trex")
    prepare.add_argument("--timeout", type=int, default=60)
    prepare.add_argument("--force", action="store_true", help="replace existing split files")
    prepare.set_defaults(func=_cmd_prepare)

    download = subparsers.add_parser("download", help="download all three official KILT T-REx files")
    download.add_argument("--data-dir", default="data/kilt/trex")
    download.add_argument("--timeout", type=int, default=60)
    download.add_argument("--force", action="store_true", help="replace existing split files")
    download.set_defaults(func=_cmd_download)

    preflight = subparsers.add_parser(
        "preflight", help="validate split sizes, row counts, schema, labels, and SHA-256 hashes"
    )
    preflight.add_argument("--data-dir", default="data/kilt/trex")
    preflight.add_argument("--manifest", help="optional output path for the dataset manifest")
    preflight.set_defaults(func=_cmd_preflight)

    train = subparsers.add_parser(
        "train", help="build the SQLite exact subject/relation/answer count index"
    )
    train.add_argument("--data-dir", default="data/kilt/trex")
    train.add_argument("--model", default="models/kilt-trex-slot-filling.sqlite")
    train.add_argument("--force", action="store_true", help="replace an existing model index")
    train.set_defaults(func=_cmd_train)

    evaluate = subparsers.add_parser(
        "evaluate", help="evaluate exact answer-surface ranking on a labeled split (use dev)"
    )
    evaluate.add_argument("--model", default="models/kilt-trex-slot-filling.sqlite")
    evaluate.add_argument("--data", default="data/kilt/trex/trex-dev-kilt.jsonl")
    evaluate.add_argument("--top-k", type=int, default=5)
    evaluate.add_argument("--output", help="optional JSON report path")
    evaluate.set_defaults(func=_cmd_evaluate)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except (GroundingDataError, OSError, urllib.error.URLError, sqlite3.Error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
