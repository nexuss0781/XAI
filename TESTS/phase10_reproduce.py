#!/usr/bin/env python3
"""Clean-build and audit the committed Phase 9 partial diagnostic.

This deliberately does not invoke Phase 9's benchmark `run` command. It rebuilds
and tests the repository, regenerates the summary from the committed JSONL, and
checks artifact hashes and frozen-manifest metadata.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
from collections import Counter
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPECTED_HASHES = {
    "TESTS/PHASE9_FREEZE.json": "b1b6dff0136f3e299a551d9074a862282347e2cb2aa8ecff161b543aaaaa1d12",
    "RESULT/Phase-9-cross-split-audit.json": "542b242966f631e42d65d34a242aa62df1d9d95342cf1b5073343787f6060901",
    "RESULT/Phase-9-public-even.jsonl": "7e83b7a28c8f6971db73593cf96e79f849aca70220e7b55bead25b303eef264e",
    "RESULT/Phase-9-public-even-partial-summary-v2.json": "85e6ac6385cb0a7ed0630d113942548e05c331f2c91ecad2a9d07a36822bf304",
    "RESULT/Phase-9-reduced-status-v2.json": "2827316efdac968c990da9b2eae13bf5b7b725ef1422e3658036a1ab6cd86922",
    "RESULT/Phase-9-stress-v2.json": "a8f67f4e01483afca530345de58e994c0e4016bb46460c63c0f202e4653b5ee7",
    "RESULT/Phase-10-stress-rerun.json": "98a79aa692baba702a832d228869d08c97eea218490971be4b7bc8f65a33d98f",
}
EXPECTED_LOCKED_FILE_COUNT = 39
EXPECTED_STATUSES = {
    "ganak": {"solved_exact": 10, "memory_limit": 6, "timeout": 3},
    "xai_full": {"solved_exact": 0, "resource_limit": 11, "timeout": 6, "memory_limit": 2},
    "xai_no_unit_propagation": {"solved_exact": 0, "resource_limit": 12, "timeout": 4, "memory_limit": 3},
    "xai_no_components": {"solved_exact": 0, "resource_limit": 13, "timeout": 4, "memory_limit": 2},
    "xai_first_branch": {"solved_exact": 0, "resource_limit": 11, "timeout": 7, "memory_limit": 1},
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()


def run(command: list[str], log: list[str], cwd: pathlib.Path = ROOT) -> subprocess.CompletedProcess[str]:
    log.append("$ " + " ".join(command))
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    log.append(result.stdout.rstrip())
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{result.stdout}")
    return result


def read_json(relative: str) -> Any:
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def verify_historical_freeze(freeze: dict[str, Any]) -> dict[str, Any]:
    revision = freeze["code_revision"]
    subprocess.run(["git", "cat-file", "-e", f"{revision}^{{commit}}"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    locked = freeze["locked_file_sha256"]
    require(len(locked) == EXPECTED_LOCKED_FILE_COUNT,
            f"expected {EXPECTED_LOCKED_FILE_COUNT} locked files, found {len(locked)}")
    checked: list[str] = []
    for relative, expected in sorted(locked.items()):
        result = subprocess.run(["git", "show", f"{revision}:{relative}"], cwd=ROOT,
                                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        actual = hashlib.sha256(result.stdout).hexdigest()
        require(actual == expected, f"frozen source hash mismatch at {revision}:{relative}")
        checked.append(relative)
    for rel, manifest_key in (("TESTS/PHASE9_PROTOCOL.md", "protocol_sha256"),
                              ("TESTS/EVALUATION_PLAN.md", "evaluation_plan_sha256")):
        result = subprocess.run(["git", "show", f"{revision}:{rel}"], cwd=ROOT,
                                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(hashlib.sha256(result.stdout).hexdigest() == freeze[manifest_key],
                f"freeze {manifest_key} mismatch at {revision}:{rel}")
    return {"revision": revision, "locked_file_count": len(checked),
            "locked_files": checked, "all_locked_hashes_match": True}


def verify_rows(rows: list[dict[str, Any]], freeze: dict[str, Any], freeze_sha: str,
                audit_sha: str, expected_summary: dict[str, Any]) -> dict[str, Any]:
    require(len(rows) == 19, f"expected 19 committed rows, found {len(rows)}")
    expected_indices = list(range(0, 37, 2))
    indices = [row.get("index") for row in rows]
    require(indices == expected_indices, "partial diagnostic must contain indices 0..36 by even steps")
    require(len(set(indices)) == len(indices), "duplicate result row index")
    statuses: dict[str, Counter[str]] = {}
    attempts = 0
    over_wall_budget = []
    for row in rows:
        require(row.get("schema") == "xai.phase9.instance-result.v1", "unexpected JSONL row schema")
        require(row.get("split") == "public-even" and row.get("smoke_run") is False,
                "unexpected split or smoke marker in committed partial diagnostic")
        require(row.get("eligibility_denominator") == 98, "unexpected eligible denominator")
        require(row.get("frozen_code_revision") == freeze["code_revision"], "row source revision differs from freeze")
        require(row.get("freeze_file_sha256") == freeze_sha, "row freeze hash mismatch")
        require(row.get("cross_split_audit_sha256") == audit_sha, "row cross-split audit hash mismatch")
        for system, outcome in row["systems"].items():
            attempts += outcome.get("system_status") != "not_run_after_mismatch"
            for key in ("stdout", "stderr"):
                digest_key = key + "_sha256"
                if outcome.get(digest_key) is not None:
                    actual = hashlib.sha256(outcome.get(key, "").encode("utf-8")).hexdigest()
                    require(actual == outcome[digest_key],
                            f"{key} hash mismatch at index {row['index']} system {system}")
            if outcome.get("wall_ms", 0) > 602_000:
                over_wall_budget.append({"index": row["index"], "system": system,
                                         "wall_ms": outcome["wall_ms"],
                                         "status": outcome["system_status"]})
        for system, outcome in row["systems"].items():
            statuses.setdefault(system, Counter())[outcome["system_status"]] += 1
    actual_statuses = {
        system: dict(sorted({**{key: 0 for key in EXPECTED_STATUSES[system]}, **counter}.items()))
        for system, counter in sorted(statuses.items())
    }
    require(actual_statuses == EXPECTED_STATUSES, "JSONL per-system outcomes differ from the reported status counts")
    require(attempts == 95, f"expected 95 attempts, found {attempts}")
    require(len(over_wall_budget) == 3, f"expected 3 attempts above 602 seconds measured wall time, found {len(over_wall_budget)}")
    require(expected_summary.get("records") == 19 and expected_summary.get("expected_records") == 98,
            "partial summary denominator or row count mismatch")
    require(expected_summary.get("evaluation_complete") is False
            and expected_summary.get("phase9_practical_gate") is False,
            "partial diagnostic must not claim Phase 9 completion")
    require(expected_summary.get("independent_oracle", {}).get("eligible_by_predeclared_cap") == 0,
            "independent oracle eligibility must match the committed diagnostic")
    return {"rows": len(rows), "indices": indices, "solver_attempts": attempts,
            "statuses": actual_statuses, "wall_budget_deviations": over_wall_budget,
            "stdout_stderr_hashes_verified": True,
            "evaluation_complete": False, "phase9_practical_gate": False}


def verify_artifacts() -> dict[str, Any]:
    artifact_hashes: dict[str, dict[str, Any]] = {}
    for relative, expected in EXPECTED_HASHES.items():
        path = ROOT / relative
        require(path.is_file(), f"required artifact missing: {relative}")
        actual = sha256(path)
        require(actual == expected, f"artifact hash mismatch: {relative}: {actual} != {expected}")
        artifact_hashes[relative] = {"sha256": actual, "size_bytes": path.stat().st_size}

    freeze_path = ROOT / "TESTS/PHASE9_FREEZE.json"
    freeze_sha = sha256(freeze_path)
    freeze = read_json("TESTS/PHASE9_FREEZE.json")
    require(freeze.get("schema") == "xai.phase9.freeze.v1", "unknown freeze schema")
    frozen_tree = verify_historical_freeze(freeze)

    audit = read_json("RESULT/Phase-9-cross-split-audit.json")
    require(audit.get("audit_complete") is True, "cross-split audit is incomplete")
    require(audit.get("frozen_code_revision") == freeze.get("code_revision"), "audit code revision differs from freeze")
    require(audit.get("freeze_file_sha256") == freeze_sha, "audit freeze hash mismatch")
    require(audit.get("archive", {}).get("sha256") == freeze.get("archive", {}).get("sha256"),
            "cross-split audit archive hash differs from freeze")
    require(audit.get("eligible_odd_claim_set_count") == 95
            and audit.get("eligible_odd_excluded_indices") == [173, 177],
            "cross-split exclusions differ from reported audit")

    summary_path = ROOT / "RESULT/Phase-9-public-even-partial-summary-v2.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    rows = [json.loads(line) for line in (ROOT / "RESULT/Phase-9-public-even.jsonl")
            .read_text(encoding="utf-8").splitlines() if line.strip()]
    row_audit = verify_rows(rows, freeze, freeze_sha, sha256(ROOT / "RESULT/Phase-9-cross-split-audit.json"), summary)

    reduced = read_json("RESULT/Phase-9-reduced-status-v2.json")
    require(reduced.get("status") == "incomplete_diagnostic"
            and reduced.get("phase9_gate") == "not_passed",
            "reduced diagnostic status artifact must remain incomplete/not passed")
    require(reduced.get("scope", {}).get("final_odd_solver_scoring") == "not performed",
            "final-odd records must remain unscored")
    require(reduced.get("outcomes", {}).get("solver_attempts") == 95,
            "reduced status attempt count differs")

    stress = read_json("RESULT/Phase-9-stress-v2.json")
    require(stress.get("passed") is True and len(stress.get("checks", [])) == 11,
            "committed synthetic stress result differs")
    require(stress.get("frozen_code_revision") == freeze.get("code_revision")
            and stress.get("freeze_file_sha256") == freeze_sha,
            "stress result provenance differs from freeze")

    stress_rerun = read_json("RESULT/Phase-10-stress-rerun.json")
    require(stress_rerun.get("schema") == "xai.phase10.synthetic-stress-rerun.v1"
            and stress_rerun.get("passed") is True
            and stress_rerun.get("check_count") == 11
            and len(stress_rerun.get("checks", [])) == 11,
            "repeated synthetic stress record is incomplete or failed")
    require(stress_rerun.get("benchmark_records_scored") == 0
            and stress_rerun.get("frozen_code_revision") == freeze.get("code_revision")
            and stress_rerun.get("freeze_manifest_sha256") == freeze_sha,
            "repeated stress record does not match the frozen non-benchmark scope")
    require(stress_rerun.get("xai_binary_sha256") == freeze["binaries"]["xai"]["sha256"]
            and stress_rerun.get("ganak_binary_sha256") == freeze["binaries"]["ganak"]["sha256"],
            "repeated stress record binary hashes differ from the freeze")

    archive_info = freeze["archive"]
    archive_path = pathlib.Path(archive_info["path_at_freeze"])
    require(archive_path.is_file(), f"frozen archive is unavailable: {archive_path}")
    require(sha256(archive_path) == archive_info["sha256"], "frozen archive hash mismatch")
    binary_verification: dict[str, Any] = {}
    for label, item in freeze["binaries"].items():
        binary_path = pathlib.Path(item["path_at_freeze"])
        require(binary_path.is_file(), f"frozen {label} executable unavailable: {binary_path}")
        actual = sha256(binary_path)
        require(actual == item["sha256"], f"frozen {label} executable hash mismatch")
        binary_verification[label] = {"path": str(binary_path), "sha256": actual, "matches_freeze": True}

    # Regenerate the derived partial summary in an isolated temporary location.
    with tempfile.TemporaryDirectory(prefix="xai-phase10-summary-") as temp:
        regenerated = pathlib.Path(temp) / "summary.json"
        command = [sys.executable, str(ROOT / "tools/phase9_eval.py"), "summarize",
                   "--input", str(ROOT / "RESULT/Phase-9-public-even.jsonl"),
                   "--output", str(regenerated)]
        proc = subprocess.run(command, cwd=ROOT, check=True, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        regenerated_bytes = regenerated.read_bytes()
        committed_bytes = summary_path.read_bytes()
        require(regenerated_bytes == committed_bytes,
                "regenerated partial summary does not byte-match committed summary")
        summarize_run = {"command": command, "stdout": proc.stdout,
                         "byte_identical_to_committed_summary": True,
                         "sha256": hashlib.sha256(regenerated_bytes).hexdigest()}

    return {"artifacts": artifact_hashes, "historical_freeze": frozen_tree,
            "row_audit": row_audit, "archive": {"path": str(archive_path),
            "sha256": sha256(archive_path), "matches_freeze": True},
            "frozen_binaries": binary_verification, "summary_regeneration": summarize_run}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path,
                        help="new or empty build directory (must not contain CMakeCache.txt)")
    parser.add_argument("--jobs", type=int, default=max(1, min(4, os.cpu_count() or 1)))
    parser.add_argument("--report", type=pathlib.Path,
                        help="optional path for a JSON verification report")
    parser.add_argument("--log", type=pathlib.Path,
                        help="optional path for a text transcript")
    args = parser.parse_args()
    require(args.jobs > 0, "--jobs must be positive")

    log: list[str] = []
    temporary_build: tempfile.TemporaryDirectory[str] | None = None
    if args.build_dir:
        build_dir = args.build_dir.resolve()
        build_dir.mkdir(parents=True, exist_ok=True)
        require(not (build_dir / "CMakeCache.txt").exists(),
                f"build directory is not clean (CMakeCache.txt exists): {build_dir}")
        require(not any(build_dir.iterdir()), f"build directory is not empty: {build_dir}")
    else:
        temporary_build = tempfile.TemporaryDirectory(prefix="xai-phase10-build-")
        build_dir = pathlib.Path(temporary_build.name)

    run(["cmake", "-S", str(ROOT), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release"], log)
    run(["cmake", "--build", str(build_dir), "--parallel", str(args.jobs)], log)
    ctest = run(["ctest", "--test-dir", str(build_dir), "--output-on-failure"], log)
    python_tests = run([sys.executable, str(ROOT / "TESTS/python/test_phase9_eval.py")], log)
    artifacts = verify_artifacts()

    report = {
        "schema": "xai.phase10.reproduction.v1",
        "completed_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "tested_commit": git("rev-parse", "HEAD"),
        "tested_worktree_clean": not bool(git("status", "--porcelain")),
        "build": {"build_type": "Release", "build_directory": str(build_dir),
                  "configure_build_ctest_passed": True, "ctest_success_summary":
                  next((line.strip() for line in ctest.stdout.splitlines() if "tests passed" in line), "see transcript"),
                  "direct_python_test_exit": python_tests.returncode},
        "artifacts": artifacts,
        "benchmark_attempts_repeated": False,
        "claim_boundary": "Build/tests and committed diagnostic artifacts were reproduced; historical solver attempts were not rerun.",
    }
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered, encoding="utf-8")
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text("\n".join(log) + "\n", encoding="utf-8")
    print(rendered, end="")
    print(f"clean build directory: {build_dir}")
    if temporary_build is not None:
        temporary_build.cleanup()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"phase10_reproduce: error: {exc}", file=sys.stderr)
        raise SystemExit(2)
