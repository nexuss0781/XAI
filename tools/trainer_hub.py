#!/usr/bin/env python3
"""Transfer sharded trainer inputs and SQLite models through the Hugging Face Hub.

The Hub repository is an artifact handoff path. This module never creates a repo,
changes its visibility, evaluates a model, or prints the HF token.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path
from typing import Any, Dict, Mapping, Tuple

import trainer

RUN_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")
REPO_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*/[A-Za-z0-9][A-Za-z0-9_.-]*$")
MERGE_RESERVE_BYTES = 2 * 1024**3


class HubTransferError(RuntimeError):
    """Raised when Hub configuration or transfer preconditions are invalid."""


def _validate_ids(repo_id: str, repo_type: str, run_id: str) -> None:
    if not REPO_ID_RE.fullmatch(repo_id):
        raise HubTransferError("repo ID must be in namespace/repository form")
    if repo_type not in {"dataset", "model"}:
        raise HubTransferError("repo type must be 'dataset' or 'model'")
    if not RUN_ID_RE.fullmatch(run_id):
        raise HubTransferError("run ID may contain only letters, digits, dots, underscores, and hyphens")


def _hub_clients() -> Tuple[Any, Any, str]:
    token = os.environ.get("HF_TOKEN")
    if not token:
        raise HubTransferError("HF_TOKEN is required in the environment")
    try:
        from huggingface_hub import HfApi, hf_hub_download
    except ImportError as exc:
        raise HubTransferError(
            "huggingface_hub is required; install the workflow-pinned dependency"
        ) from exc
    return HfApi(token=token), hf_hub_download, token


def _plan_branch(run_id: str) -> str:
    return f"trainer-{run_id}-plan"


def _worker_branch(run_id: str, shard_index: int) -> str:
    return f"trainer-{run_id}-worker-{shard_index:04d}"


def _run_prefix(run_id: str) -> str:
    return f"runs/{run_id}"


def _model_repo_path(run_id: str, shard_index: int) -> str:
    return f"{_run_prefix(run_id)}/workers/shard-{shard_index:04d}.sqlite"


def _download_manifest(
    hf_hub_download: Any,
    repo_id: str,
    repo_type: str,
    run_id: str,
    token: str,
    work_dir: Path,
) -> Tuple[Path, Dict[str, Any]]:
    remote_manifest = (
        f"{_run_prefix(run_id)}/{trainer.RUN_MANIFEST_FILENAME}"
    )
    cached_manifest = hf_hub_download(
        repo_id=repo_id,
        filename=remote_manifest,
        repo_type=repo_type,
        revision=_plan_branch(run_id),
        token=token,
    )
    run_dir = work_dir / "run"
    run_dir.mkdir(parents=True, exist_ok=True)
    local_manifest = run_dir / trainer.RUN_MANIFEST_FILENAME
    local_manifest.write_bytes(Path(cached_manifest).read_bytes())
    manifest, _ = trainer._load_run_manifest(run_dir)
    return run_dir, manifest


def _verify_local_plan(run_dir: Path, manifest: Mapping[str, Any]) -> None:
    for shard in manifest["shards"]:
        shard_path = run_dir / "shards" / shard["filename"]
        if not shard_path.is_file():
            raise HubTransferError(f"planned shard is missing: {shard_path}")
        digest, size = trainer._sha256_file(shard_path)
        if size != shard["bytes"] or digest != shard["sha256"]:
            raise HubTransferError(f"planned shard changed after planning: {shard_path}")


def upload_plan(args: argparse.Namespace) -> Dict[str, Any]:
    _validate_ids(args.repo_id, args.repo_type, args.run_id)
    run_dir = Path(args.run_dir).resolve()
    manifest, manifest_sha256 = trainer._load_run_manifest(run_dir)
    _verify_local_plan(run_dir, manifest)
    api, _, token = _hub_clients()
    branch = _plan_branch(args.run_id)
    api.create_branch(
        repo_id=args.repo_id,
        branch=branch,
        revision=args.base_revision,
        token=token,
        repo_type=args.repo_type,
        exist_ok=True,
    )
    files_to_upload = [
        (
            run_dir / trainer.RUN_MANIFEST_FILENAME,
            f"{_run_prefix(args.run_id)}/{trainer.RUN_MANIFEST_FILENAME}",
        )
    ]
    files_to_upload.extend(
        (
            run_dir / "shards" / shard["filename"],
            f"{_run_prefix(args.run_id)}/shards/{shard['filename']}",
        )
        for shard in manifest["shards"]
    )
    for local_path, remote_path in files_to_upload:
        api.upload_file(
            path_or_fileobj=str(local_path),
            path_in_repo=remote_path,
            repo_id=args.repo_id,
            repo_type=args.repo_type,
            revision=branch,
            token=token,
            commit_message=f"Add trainer run {args.run_id} file {local_path.name}",
        )
    result = {
        "repo_id": args.repo_id,
        "repo_type": args.repo_type,
        "run_id": args.run_id,
        "plan_branch": branch,
        "manifest_sha256": manifest_sha256,
        "shard_count": manifest["shard_count"],
        "training_bytes": manifest["source"]["bytes"],
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return result


def train_remote_shard(args: argparse.Namespace) -> Dict[str, Any]:
    _validate_ids(args.repo_id, args.repo_type, args.run_id)
    api, hf_hub_download, token = _hub_clients()
    work_dir = Path(args.work_dir).resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    run_dir, manifest = _download_manifest(
        hf_hub_download, args.repo_id, args.repo_type, args.run_id, token, work_dir
    )
    if args.shard_index < 0 or args.shard_index >= manifest["shard_count"]:
        raise HubTransferError(
            f"shard index must be in 0..{manifest['shard_count'] - 1}"
        )
    shard = manifest["shards"][args.shard_index]
    shard_cache_path = hf_hub_download(
        repo_id=args.repo_id,
        filename=f"{_run_prefix(args.run_id)}/shards/{shard['filename']}",
        repo_type=args.repo_type,
        revision=_plan_branch(args.run_id),
        token=token,
    )
    model_path = work_dir / "models" / f"shard-{args.shard_index:04d}.sqlite"
    model_path.parent.mkdir(parents=True, exist_ok=True)
    metadata = trainer.train_shard(
        run_dir,
        args.shard_index,
        model_path=model_path,
        shard_path=Path(shard_cache_path),
    )

    branch = _worker_branch(args.run_id, args.shard_index)
    api.create_branch(
        repo_id=args.repo_id,
        branch=branch,
        revision=_plan_branch(args.run_id),
        token=token,
        repo_type=args.repo_type,
        exist_ok=True,
    )
    remote_model = _model_repo_path(args.run_id, args.shard_index)
    api.upload_file(
        path_or_fileobj=str(model_path),
        path_in_repo=remote_model,
        repo_id=args.repo_id,
        repo_type=args.repo_type,
        revision=branch,
        token=token,
        commit_message=f"Store trainer run {args.run_id} shard {args.shard_index:04d}",
    )
    result = {
        "repo_id": args.repo_id,
        "repo_type": args.repo_type,
        "run_id": args.run_id,
        "shard_index": args.shard_index,
        "worker_branch": branch,
        "model_path_in_repo": remote_model,
        "model_sha256": trainer._sha256_file(model_path)[0],
        "model_bytes": model_path.stat().st_size,
        "training_records": metadata["training_records"],
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return result


def _remote_model_size(
    api: Any, repo_id: str, repo_type: str, run_id: str, shard_index: int, token: str
) -> int:
    entries = api.get_paths_info(
        repo_id=repo_id,
        paths=[_model_repo_path(run_id, shard_index)],
        repo_type=repo_type,
        revision=_worker_branch(run_id, shard_index),
        token=token,
    )
    if len(entries) != 1 or not isinstance(getattr(entries[0], "size", None), int):
        raise HubTransferError(f"cannot determine remote model size for shard {shard_index}")
    return entries[0].size


def merge_remote_shards(args: argparse.Namespace) -> Dict[str, Any]:
    _validate_ids(args.repo_id, args.repo_type, args.run_id)
    api, hf_hub_download, token = _hub_clients()
    work_dir = Path(args.work_dir).resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    run_dir, manifest = _download_manifest(
        hf_hub_download, args.repo_id, args.repo_type, args.run_id, token, work_dir
    )

    shard_count = manifest["shard_count"]
    remote_sizes = [
        _remote_model_size(api, args.repo_id, args.repo_type, args.run_id, index, token)
        for index in range(shard_count)
    ]
    total_partial_bytes = sum(remote_sizes)
    # Leave room for the merged SQLite file, its transaction journal, the largest
    # currently downloaded partial, and runner/application headroom. Refuse before
    # downloading any model if the hosted runner cannot meet this conservative bound.
    required_free_bytes = (
        2 * total_partial_bytes + max(remote_sizes, default=0) + MERGE_RESERVE_BYTES
    )
    available_bytes = shutil.disk_usage(work_dir).free
    if available_bytes < required_free_bytes:
        raise HubTransferError(
            "insufficient merge-runner disk before downloads: "
            f"need a conservative {required_free_bytes:,} bytes, "
            f"have {available_bytes:,} bytes"
        )

    cache_dirs: Dict[int, Path] = {}

    def load_one(index: int, _shard: Mapping[str, Any]) -> Path:
        cache_dir = work_dir / "hf-cache" / f"worker-{index:04d}"
        cache_dirs[index] = cache_dir
        try:
            cached_path = hf_hub_download(
                repo_id=args.repo_id,
                filename=_model_repo_path(args.run_id, index),
                repo_type=args.repo_type,
                revision=_worker_branch(args.run_id, index),
                token=token,
                cache_dir=str(cache_dir),
            )
            return Path(cached_path)
        except BaseException:
            shutil.rmtree(cache_dir, ignore_errors=True)
            raise

    def release_one(index: int, _path: Path) -> None:
        cache_dir = cache_dirs.pop(index, None)
        if cache_dir is not None:
            shutil.rmtree(cache_dir, ignore_errors=True)

    output_model = Path(args.output_model).resolve()
    output_model.parent.mkdir(parents=True, exist_ok=True)
    result = trainer.merge_shards(
        run_dir,
        output_model=output_model,
        shard_model_loader=load_one,
        shard_model_cleanup=release_one,
    )
    merged_path = f"{_run_prefix(args.run_id)}/merged/kilt-trex-slot-filling-merged.sqlite"
    api.upload_file(
        path_or_fileobj=str(output_model),
        path_in_repo=merged_path,
        repo_id=args.repo_id,
        repo_type=args.repo_type,
        revision=_plan_branch(args.run_id),
        token=token,
        commit_message=f"Synthesize trainer run {args.run_id} merged model",
    )
    result.update(
        {
            "repo_id": args.repo_id,
            "repo_type": args.repo_type,
            "run_id": args.run_id,
            "plan_branch": _plan_branch(args.run_id),
            "merged_path_in_repo": merged_path,
            "worker_model_branches": [
                _worker_branch(args.run_id, index) for index in range(shard_count)
            ],
            "individual_shards_evaluated": False,
        }
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return result


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Move trainer shards and SQLite models through a Hugging Face Hub repo."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    plan = subparsers.add_parser("upload-plan", help="upload one run's manifest and shard files")
    plan.add_argument("--repo-id", required=True)
    plan.add_argument("--repo-type", choices=("dataset", "model"), required=True)
    plan.add_argument("--run-id", required=True)
    plan.add_argument("--run-dir", type=Path, required=True)
    plan.add_argument("--base-revision", default="main")
    plan.set_defaults(handler=upload_plan)

    worker = subparsers.add_parser("train-shard", help="download, train, and upload one shard")
    worker.add_argument("--repo-id", required=True)
    worker.add_argument("--repo-type", choices=("dataset", "model"), required=True)
    worker.add_argument("--run-id", required=True)
    worker.add_argument("--shard-index", type=int, required=True)
    worker.add_argument("--work-dir", type=Path, required=True)
    worker.set_defaults(handler=train_remote_shard)

    merge = subparsers.add_parser("merge", help="download partials one at a time, synthesize, upload")
    merge.add_argument("--repo-id", required=True)
    merge.add_argument("--repo-type", choices=("dataset", "model"), required=True)
    merge.add_argument("--run-id", required=True)
    merge.add_argument("--work-dir", type=Path, required=True)
    merge.add_argument("--output-model", type=Path, required=True)
    merge.set_defaults(handler=merge_remote_shards)
    return parser


def main(argv: Any = None) -> int:
    args = _parser().parse_args(argv)
    try:
        args.handler(args)
    except (HubTransferError, trainer.grounding.GroundingDataError, OSError, ValueError) as exc:
        print(f"trainer_hub: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:  # Includes sanitized Hugging Face client/auth/network errors.
        print(f"trainer_hub: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
