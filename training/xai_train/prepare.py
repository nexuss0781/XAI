"""Prepare leakage-resistant train/dev/test manifests; this module never trains a model."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any, Iterable

from .records import require_training_rights, validate_record


def _normalized_text(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip().casefold()


def _digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    seen_ids: set[str] = set()
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                record = validate_record(json.loads(line))
                require_training_rights(record)
            except (json.JSONDecodeError, ValueError) as exc:
                raise ValueError(f"{path}:{line_number}: {exc}") from exc
            if record["record_id"] in seen_ids:
                raise ValueError(f"duplicate record_id: {record['record_id']}")
            seen_ids.add(record["record_id"])
            records.append(record)
    if not records:
        raise ValueError("input JSONL contains no records")
    return records


class _Groups:
    def __init__(self, keys: Iterable[str]) -> None:
        self.parent = {key: key for key in keys}

    def find(self, key: str) -> str:
        parent = self.parent[key]
        if parent != key:
            self.parent[key] = self.find(parent)
        return self.parent[key]

    def union(self, left: str, right: str) -> None:
        a, b = self.find(left), self.find(right)
        if a != b:
            self.parent[max(a, b)] = min(a, b)


def prepare(records: list[dict[str, Any]], output_dir: Path, seed: int,
            target_language: str | None = None) -> dict[str, Any]:
    records = [validate_record(record) for record in records]
    for record in records:
        require_training_rights(record)
    if not records:
        raise ValueError("input records are empty")
    groups = _Groups(record["source_id"] for record in records)
    first_for_key: dict[str, str] = {}
    for record in records:
        source = record["source_id"]
        keys = ["exact:" + hashlib.sha256(_normalized_text(record["text"]).encode("utf-8")).hexdigest()]
        if record.get("duplicate_group"):
            keys.append("declared:" + record["duplicate_group"])
        for key in keys:
            if key in first_for_key:
                groups.union(source, first_for_key[key])
            else:
                first_for_key[key] = source

    roots: dict[str, list[str]] = {}
    for source in {record["source_id"] for record in records}:
        roots.setdefault(groups.find(source), []).append(source)
    assignment: dict[str, str] = {}
    for root, sources in roots.items():
        stable_key = "\0".join(sorted(sources))
        value = int(hashlib.sha256(f"{seed}\0{stable_key}".encode()).hexdigest()[:8], 16) / 0xFFFFFFFF
        assignment[root] = "train" if value < 0.8 else ("dev" if value < 0.9 else "test")
    source_split = {source: assignment[groups.find(source)] for source in {r["source_id"] for r in records}}

    def language_matches(record: dict[str, Any]) -> bool:
        return target_language is None or record["language"].casefold() == target_language.casefold()

    pretrain: list[dict[str, Any]] = []
    graph_train: list[dict[str, Any]] = []
    development: list[dict[str, Any]] = []
    final_test: list[dict[str, Any]] = []
    for record in records:
        split = source_split[record["source_id"]]
        if split == "train" and record["use_pretraining"]:
            # Strip annotation targets so the denoising stage cannot ingest graph labels.
            pretrain.append({**record, "accepted_readings": [], "use_graph_training": False})
        if split == "train" and record["use_graph_training"] and not (
            target_language is not None and record["language"].casefold() == target_language.casefold()
        ):
            graph_train.append(record)
        if split == "dev" and record["use_graph_training"] and not (
            target_language is not None and record["language"].casefold() == target_language.casefold()
        ):
            development.append(record)
        if split == "test" and record["use_graph_training"] and language_matches(record):
            final_test.append(record)

    if not pretrain:
        raise ValueError("no approved training-partition text is eligible for denoising pretraining")
    if not graph_train:
        raise ValueError("no approved graph-training examples remain after source/target-language filtering")
    if not development:
        raise ValueError("no approved non-target development graphs; cannot select a checkpoint")
    if not final_test:
        raise ValueError("no approved final-test graphs in the selected language(s)")

    output_dir.mkdir(parents=True, exist_ok=True)
    content_files = {
        "pretrain_train.jsonl": pretrain,
        "graph_train.jsonl": graph_train,
        "graph_dev.jsonl": development,
        "graph_test.jsonl": final_test,
    }
    file_metadata: dict[str, dict[str, Any]] = {}
    for name, values in content_files.items():
        path = output_dir / name
        with path.open("w", encoding="utf-8", newline="\n") as stream:
            for value in values:
                stream.write(json.dumps(value, ensure_ascii=False, sort_keys=True) + "\n")
        file_metadata[name] = {"records": len(values), "sha256": _digest(path)}

    manifest = {
        "schema_version": 1,
        "seed": seed,
        "target_language": target_language,
        "grouping": "source_id + declared duplicate_group + normalized exact-text hash",
        "fractions": {"train": 0.8, "dev": 0.1, "test": 0.1},
        "group_assignments": dict(sorted(source_split.items())),
        "records_by_split": {name: sum(1 for record in records if source_split[record["source_id"]] == name) for name in ("train", "dev", "test")},
        "graph_train_languages": sorted({r["language"] for r in graph_train}),
        "graph_dev_languages": sorted({r["language"] for r in development}),
        "graph_test_languages": sorted({r["language"] for r in final_test}),
        "files": file_metadata,
        "training_started": False,
    }
    manifest_path = output_dir / "split-manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    manifest["manifest_sha256"] = _digest(manifest_path)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="approved XAI training-schema JSONL")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--seed", type=int, default=20261008)
    parser.add_argument("--target-language", help="predeclare a zero-shot graph-evaluation language")
    args = parser.parse_args()
    result = prepare(_read_jsonl(args.input), args.output_dir, args.seed, args.target_language)
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
