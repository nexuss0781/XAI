#!/usr/bin/env python3
"""Write a SHA-256 manifest for tracked source plus explicitly supplied inputs."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
from typing import Iterable

FORMAT_VERSION = 1


def run_git(root: pathlib.Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def digest(path: pathlib.Path) -> dict[str, object]:
    hasher = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            hasher.update(chunk)
            size += len(chunk)
    return {"sha256": hasher.hexdigest(), "size_bytes": size}


def entries(paths: Iterable[pathlib.Path], root: pathlib.Path) -> dict[str, object]:
    result: dict[str, object] = {}
    for path in sorted(paths, key=lambda p: p.as_posix()):
        resolved = path if path.is_absolute() else root / path
        if not resolved.is_file():
            raise FileNotFoundError(f"manifest input is not a regular file: {path}")
        try:
            label = resolved.resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            label = resolved.name
        if label in result:
            raise ValueError(f"duplicate manifest label: {label}")
        result[label] = digest(resolved)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--data", action="append", default=[], type=pathlib.Path,
                        help="data file to hash; may be repeated")
    parser.add_argument("--config", action="append", default=[], type=pathlib.Path,
                        help="configuration file to hash; may be repeated")
    args = parser.parse_args()
    root = args.root.resolve()
    tracked = [root / name for name in run_git(root, "ls-files", "-z").split("\0") if name]
    revision = run_git(root, "rev-parse", "HEAD")
    dirty = bool(run_git(root, "status", "--porcelain", "--untracked-files=no"))
    untracked = bool(run_git(root, "ls-files", "--others", "--exclude-standard", "-z"))
    manifest = {
        "manifest_format_version": FORMAT_VERSION,
        "source_revision": revision,
        "tracked_worktree_modified": dirty,
        "untracked_files_present": untracked,
        "source_files": entries(tracked, root),
        "data_inputs": entries(args.data, root),
        "configuration_inputs": entries(args.config, root),
        "limits": {
            "data_and_config_contents_are_not_embedded": True,
            "ignored_or_untracked_files_are_not_in_source_files": True,
            "explicitly_supplied_inputs_are_hashed_by_content": True,
        },
    }
    output = args.output if args.output.is_absolute() else root / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                                 separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"wrote {output} ({len(tracked)} tracked source files, "
          f"{len(args.data)} data inputs, {len(args.config)} config inputs)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"manifest error: {error}", file=sys.stderr)
        raise SystemExit(2)
