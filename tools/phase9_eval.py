#!/usr/bin/env python3
"""Frozen Phase 9 runner for direct exact-rational WMC evaluation.

The `audit` command is intentionally the first command that decompresses odd-indexed
members. It refuses to do so unless the exact source/configuration freeze verifies.
The `run --split final-odd` command additionally requires the completed audit artifact.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import lzma
import os
import platform
import re
import signal
import statistics
import subprocess
import sys
import tarfile
import tempfile
import time
from collections import Counter
from fractions import Fraction
from pathlib import Path
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_DEFAULT = ROOT / "data/raw/mc2024-track2-wmc_competition.tar"
PUBLIC_AUDIT_DEFAULT = ROOT / "RESULT/wmc-public-audit.json"
FREEZE_DEFAULT = ROOT / "TESTS/PHASE9_FREEZE.json"
MEMBER_RE = re.compile(r"(?:^|/)mc2024_track2-random_(\d+)\.cnf\.xz$")
EXPECTED_MD5 = "1c7e6279eaf29bf3731cebe07eedf107"
MAX_RAW_BYTES = 4 * 1024 * 1024 * 1024
ORACLE_MAX_VARIABLES = 20
ORACLE_MAX_WORK = 50_000_000
CPU_BUDGET_SECONDS = 600
MEMORY_BUDGET_BYTES = 4 * 1024 * 1024 * 1024
XAI_BASE_OPTIONS = ["--timeout-ms", "600000", "--node-limit", "10000000", "--max-depth", "4096"]
XAI_CONFIGS = [
    {"name": "xai_full", "args": []},
    {"name": "xai_no_unit_propagation", "args": ["--no-unit-propagation"]},
    {"name": "xai_no_components", "args": ["--no-components"]},
    {"name": "xai_first_branch", "args": ["--first-branch"]},
]
GANAK_OPTIONS = ["--mode", "1", "--prob", "0", "--seed", "1", "--maxcache=16000",
                 "--threads", "1", "--verb", "0"]
LOCKED_PATHS = [
    "CMakeLists.txt",
    "Phase.md",
    "TODO.md",
    "TESTS/EVALUATION_PLAN.md",
    "TESTS/PHASE9_PROTOCOL.md",
    "RESULT/Phase-0.md",
]


def now_utc() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def md5_file(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_public_auditor():
    path = ROOT / "tools/audit_wmc_archive.py"
    spec = importlib.util.spec_from_file_location("xai_phase0_auditor", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load the frozen public archive parser")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def git(*args: str, check: bool = True) -> str:
    completed = subprocess.run(["git", *args], cwd=ROOT, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and completed.returncode:
        raise RuntimeError(f"git {' '.join(args)} failed: {completed.stderr.strip()}")
    return completed.stdout.strip()


def locked_path_list() -> list[str]:
    tracked = git("ls-files", "-z").split("\0")
    code_prefixes = ("include/", "src/", "tools/", "TESTS/cpp/", "TESTS/python/")
    selected = set(LOCKED_PATHS)
    selected.update(path for path in tracked if path.startswith(code_prefixes))
    return sorted(selected)


def tool_version(command: list[str]) -> str:
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=15, check=False)
        return result.stdout.strip().splitlines()[0] if result.stdout.strip() else f"exit={result.returncode}"
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"unavailable: {exc}"


def dependency_revisions(source: Path, build: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in sorted((build / "_deps").glob("*-src")):
        if (path / ".git").exists():
            dirty = subprocess.run(["git", "-C", str(path), "status", "--porcelain"],
                                   text=True, stdout=subprocess.PIPE, check=True).stdout.strip()
            if dirty:
                raise ValueError(f"fetched Ganak dependency source is dirty: {path.name}")
            rev = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"],
                                 text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                 check=False)
            if rev.returncode == 0:
                result[path.name.removesuffix("-src")] = rev.stdout.strip()
    if (source / ".git").exists():
        result["ganak"] = subprocess.run(
            ["git", "-C", str(source), "rev-parse", "HEAD"], text=True,
            stdout=subprocess.PIPE, check=True).stdout.strip()
    return result


def cpu_description() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
            if line.lower().startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def protocol_options() -> dict[str, Any]:
    return {
        "xai": {
            "binary_args": XAI_BASE_OPTIONS,
            "full_solver": "unit propagation + component decomposition + occurrence branching",
            "ablations": XAI_CONFIGS[1:],
            "parser_limits": {
                "max_variables": 1_000_000,
                "max_clauses": 10_000_000,
                "max_literal_occurrences": 100_000_000,
                "max_line_bytes": 16 * 1024 * 1024,
                "max_input_bytes": 4 * 1024 * 1024 * 1024,
            },
        },
        "ganak": {
            "version": "2.7.0",
            "commit": "e8f51841832efbbad4c1dc30e7e628ea39e8eec6",
            "binary_args": GANAK_OPTIONS,
            "threads": 1,
            "note": "--mode 1 exact rationals; --prob 0 disables probabilistic hashing; --maxcache=16000 follows Ganak's exact MCC runner; seed fixed at 1.",
        },
        "external_per_process_limits": {
            "wall_seconds_including_solver_parse": CPU_BUDGET_SECONDS,
            "cpu_seconds_soft_hard": [CPU_BUDGET_SECONDS + 1, CPU_BUDGET_SECONDS + 2],
            "cpu_affinity": "one logical CPU",
            "resident_memory_bytes": MEMORY_BUDGET_BYTES,
            "memory_enforcement": "10-ms /proc process-group RSS monitor; kill process group at/above 4 GiB; peak RSS recorded",
        },
        "input_handling": "decompress XZ transport once to a temporary file; pass identical unmodified decompressed formula bytes to all systems",
        "randomness": {"xai": "deterministic", "ganak_seed": 1},
    }


def ensure_archive(path: Path, expected_sha256: str | None = None) -> dict[str, Any]:
    if not path.is_file():
        raise ValueError(f"archive not found: {path}")
    md5 = md5_file(path)
    if md5 != EXPECTED_MD5:
        raise ValueError(f"archive MD5 mismatch: expected {EXPECTED_MD5}, got {md5}")
    sha = sha256_file(path)
    if expected_sha256 and sha != expected_sha256:
        raise ValueError(f"archive SHA-256 differs from frozen snapshot: {sha}")
    return {"path_at_freeze": str(path), "md5": md5, "sha256": sha, "size_bytes": path.stat().st_size}


def make_freeze(args: argparse.Namespace) -> None:
    output = args.output.resolve()
    if git("status", "--porcelain"):
        raise ValueError("freeze requires a clean committed worktree")
    revision = git("rev-parse", "HEAD")
    if git("rev-parse", "--is-shallow-repository") == "true":
        raise ValueError("freeze requires complete source history")
    frozen_paths = locked_path_list()
    missing = [p for p in frozen_paths if not (ROOT / p).is_file()]
    if missing:
        raise ValueError(f"locked files are missing: {missing}")
    xai = args.xai.resolve()
    ganak = args.ganak.resolve()
    source = args.ganak_source.resolve()
    if not xai.is_file() or not ganak.is_file():
        raise ValueError("both built solver binaries must exist before the freeze")
    base_commit = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                                 text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 check=True).stdout.strip()
    expected_commit = "e8f51841832efbbad4c1dc30e7e628ea39e8eec6"
    if base_commit != expected_commit:
        raise ValueError(f"Ganak checkout is {base_commit}, expected {expected_commit}")
    if subprocess.run(["git", "-C", str(source), "status", "--porcelain"],
                      text=True, stdout=subprocess.PIPE, check=True).stdout.strip():
        raise ValueError("pinned Ganak source checkout is dirty")
    build = args.ganak_build.resolve()
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    if ("CMAKE_BUILD_TYPE:STRING=Release" not in cache or
            "CMAKE_CXX_STANDARD_LIBRARIES:UNINITIALIZED=-lflint" not in cache):
        raise ValueError("Ganak build must be Release and include the recorded explicit -lflint build workaround")
    files = {p: sha256_file(ROOT / p) for p in frozen_paths}
    archive = ensure_archive(args.archive.resolve())
    manifest = {
        "schema": "xai.phase9.freeze.v1",
        "created_utc": now_utc(),
        "code_revision": revision,
        "code_tree": git("rev-parse", "HEAD^{tree}"),
        "locked_file_sha256": files,
        "protocol_sha256": sha256_file(ROOT / "TESTS/PHASE9_PROTOCOL.md"),
        "evaluation_plan_sha256": sha256_file(ROOT / "TESTS/EVALUATION_PLAN.md"),
        "archive": archive,
        "options": protocol_options(),
        "binaries": {
            "xai": {"path_at_freeze": str(xai), "sha256": sha256_file(xai),
                    "version": tool_version([str(xai), "--help"])},
            "ganak": {"path_at_freeze": str(ganak), "sha256": sha256_file(ganak),
                      "version": tool_version([str(ganak), "--version"])},
        },
        "ganak_source": {"repository": "https://github.com/meelgroup/ganak",
                         "path_at_freeze": str(source), "commit": base_commit,
                         "build_path_at_freeze": str(build),
                         "dependency_commits": dependency_revisions(source, build)},
        "ganak_build": {
            "cmake_args": ["-DCMAKE_BUILD_TYPE=Release", "-DENABLE_TESTING=OFF",
                           "-DBUILD_PYTHON_EXTENSION=OFF", "-DCMAKE_CXX_STANDARD_LIBRARIES=-lflint"],
            "build_args": ["--parallel", "2", "--target", "ganak-bin"],
            "cmake_cache_sha256": sha256_file(build / "CMakeCache.txt"),
            "link_command_sha256": sha256_file(build / "src/CMakeFiles/ganak-bin.dir/link.txt"),
            "build_only_note": "Ubuntu FLINT pkg-config omitted -lflint; explicit standard-library link flag resolved the pinned CLI symbols without modifying Ganak source.",
        },
        "environment": {
            "platform": platform.platform(),
            "python": sys.version.split()[0],
            "compiler": tool_version(["g++", "--version"]),
            "cmake": tool_version(["cmake", "--version"]),
            "gmp": tool_version(["pkg-config", "--modversion", "gmp"]),
            "mpfr": tool_version(["pkg-config", "--modversion", "mpfr"]),
            "flint": tool_version(["pkg-config", "--modversion", "flint"]),
            "cpu": cpu_description(),
            "logical_cpu_affinity_available": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else [],
        },
        "predeclared_analysis": {
            "correctness": "exact Fraction equality for direct XAI/Ganak successful counts; any mismatch stops the run and invalidates the exactness claim",
            "utility": "completed exact results / exact-weight-eligible benchmark members; report each noncompletion category separately",
            "inference": "named challenge archive only; descriptive finite-set counts/rates; no population confidence intervals or generalization claims",
            "no_tuning": True,
            "stopping_rule": "finish all eligible records/configurations unless a count mismatch occurs; any mismatch halts immediately; individual attempt is capped at 600 s wall and CPU, one CPU and 4 GiB RSS",
            "calibration_or_predictive_scores": "not estimable: benchmark has no outcome labels or calibration split",
            "claim_scope": "directly compared exact WMC solver outputs only; no certificate claim from the Phase 8 synthetic verifier",
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"freeze_file": str(output), "code_revision": revision,
                      "locked_files": len(files), "archive_sha256": archive["sha256"],
                      "xai_sha256": manifest["binaries"]["xai"]["sha256"],
                      "ganak_sha256": manifest["binaries"]["ganak"]["sha256"]}, indent=2))


def load_freeze(path: Path) -> dict[str, Any]:
    try:
        freeze = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read freeze manifest: {exc}") from exc
    if freeze.get("schema") != "xai.phase9.freeze.v1":
        raise ValueError("unsupported/missing Phase 9 freeze schema")
    return freeze


def verify_freeze(path: Path, archive: Path, xai: Path | None = None,
                  ganak: Path | None = None) -> dict[str, Any]:
    freeze = load_freeze(path)
    revision = freeze["code_revision"]
    git("cat-file", "-e", f"{revision}^{{commit}}")
    for rel, expected in freeze["locked_file_sha256"].items():
        current = ROOT / rel
        if not current.is_file() or sha256_file(current) != expected:
            raise ValueError(f"frozen file changed after freeze: {rel}")
        at_commit = subprocess.run(["git", "show", f"{revision}:{rel}"], cwd=ROOT,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if at_commit.returncode or sha256_bytes(at_commit.stdout) != expected:
            raise ValueError(f"frozen commit does not contain expected bytes for {rel}")
    ensure_archive(archive, freeze["archive"]["sha256"])
    for label, binary, manifest_key in (("xai", xai, "xai"), ("ganak", ganak, "ganak")):
        expected_binary = freeze["binaries"][manifest_key]
        candidate = binary.resolve() if binary else Path(expected_binary["path_at_freeze"])
        if not candidate.is_file() or sha256_file(candidate) != expected_binary["sha256"]:
            raise ValueError(f"{label} binary differs from frozen executable hash")
    return freeze


def list_members(archive_path: Path) -> dict[int, tarfile.TarInfo]:
    members: dict[int, tarfile.TarInfo] = {}
    with tarfile.open(archive_path, mode="r:") as archive:
        for member in archive:
            match = MEMBER_RE.search(member.name)
            if not match or not member.isfile():
                continue
            index = int(match.group(1))
            if index in members:
                raise ValueError(f"duplicate archive index {index}")
            members[index] = member
    if set(members) != set(range(200)):
        raise ValueError("archive must contain exactly one matching member for every index 0..199")
    return members


def parse_member(archive_path: Path, member: tarfile.TarInfo, parser: Any) -> dict[str, Any]:
    digest = hashlib.sha256()
    raw_count = 0
    with tarfile.open(archive_path, mode="r:") as archive:
        source = archive.extractfile(member)
        if source is None:
            raise ValueError(f"cannot read archive member {member.name}")

        def lines() -> Iterable[str]:
            nonlocal raw_count
            with lzma.LZMAFile(source, mode="rb") as decompressor:
                for raw in decompressor:
                    raw_count += len(raw)
                    if raw_count > MAX_RAW_BYTES:
                        raise ValueError(f"decompressed member exceeds 4-GiB cap: {member.name}")
                    digest.update(raw)
                    yield raw.decode("utf-8", errors="strict")

        try:
            parsed = parser(lines())
            parsed.update({"machine_parseable": True, "parse_error": None})
        except (ValueError, UnicodeDecodeError, EOFError, lzma.LZMAError) as exc:
            parsed = {"machine_parseable": False, "parse_error": str(exc), "weight_errors": [],
                      "provenance": [], "clause_hashes": set(), "canonical_formula_sha256": None}
    parsed.update({"index": int(MEMBER_RE.search(member.name).group(1)),
                   "member": member.name, "compressed_bytes": member.size,
                   "decompressed_bytes": raw_count, "raw_sha256": digest.hexdigest()})
    return parsed


def eligible(row: dict[str, Any]) -> bool:
    return bool(row.get("machine_parseable")) and not row.get("weight_errors")


def normalized_sources(row: dict[str, Any]) -> set[str]:
    return {" ".join(str(x).split()) for x in row.get("provenance", []) if str(x).strip()}


def near_duplicate(left: dict[str, Any], right: dict[str, Any]) -> float | None:
    if not left.get("machine_parseable") or not right.get("machine_parseable"):
        return None
    if left.get("nvars") != right.get("nvars"):
        return None
    a, b = left.get("clause_hashes", set()), right.get("clause_hashes", set())
    if not a and not b:
        return 1.0
    smaller, larger = (a, b) if len(a) <= len(b) else (b, a)
    if larger and len(smaller) / len(larger) < 0.95:
        return 0.0
    intersection = len(smaller & larger)
    union = len(a) + len(b) - intersection
    return intersection / union if union else 1.0


def audit_splits(args: argparse.Namespace) -> None:
    archive_path = args.archive.resolve()
    freeze_path = args.freeze.resolve()
    freeze = verify_freeze(freeze_path, archive_path)
    if git("status", "--porcelain"):
        raise ValueError("cross-split audit requires a clean worktree after committing the freeze")
    parser = load_public_auditor().parse_instance
    members = list_members(archive_path)
    public_rows: list[dict[str, Any]] = []
    odd_rows: list[dict[str, Any]] = []
    for index in range(200):
        # This is the first operation in the workflow that decompresses any odd member.
        row = parse_member(archive_path, members[index], parser)
        (odd_rows if index % 2 else public_rows).append(row)
        if (index + 1) % 20 == 0:
            print(f"cross-split audit parsed {index + 1}/200 formulas; freeze verified", flush=True)

    public_baseline = json.loads(PUBLIC_AUDIT_DEFAULT.read_text(encoding="utf-8"))
    old_eligible = {int(r["index"]) for r in public_baseline["files"]
                    if r.get("machine_parseable") and not r.get("weight_errors")}
    new_eligible = {r["index"] for r in public_rows if eligible(r)}
    if old_eligible != new_eligible or len(new_eligible) != 98:
        raise ValueError("recomputed public eligibility does not match the frozen 98-item Phase 0 audit")

    links: dict[tuple[int, int], set[str]] = {}
    for left in public_rows:
        for right in odd_rows:
            reasons: set[str] = set()
            if left.get("raw_sha256") == right.get("raw_sha256"):
                reasons.add("identical_decompressed_bytes_sha256")
            if (left.get("canonical_formula_sha256") and
                    left.get("canonical_formula_sha256") == right.get("canonical_formula_sha256")):
                reasons.add("identical_canonical_formula_sha256")
            if normalized_sources(left) & normalized_sources(right):
                reasons.add("shared_provenance_tag")
            similarity = near_duplicate(left, right)
            if similarity is not None and similarity >= 0.95 and not (
                    left.get("canonical_formula_sha256") == right.get("canonical_formula_sha256")):
                reasons.add("near_duplicate_clause_jaccard_ge_0.95")
            if reasons:
                links[(left["index"], right["index"])] = reasons

    eligible_odd = {r["index"] for r in odd_rows if eligible(r)}
    excluded_odd = sorted({right for (_, right) in links} & eligible_odd)
    pair_rows = []
    for (left, right), reasons in sorted(links.items()):
        lrow = next(x for x in public_rows if x["index"] == left)
        rrow = next(x for x in odd_rows if x["index"] == right)
        similarity = near_duplicate(lrow, rrow)
        pair_rows.append({"public_index": left, "public_member": lrow["member"],
                          "odd_index": right, "odd_member": rrow["member"],
                          "reasons": sorted(reasons),
                          "clause_jaccard": similarity if similarity is not None and similarity >= 0.95 else None})
    odd_eligibility = [{"index": r["index"], "member": r["member"],
                        "machine_parseable": r["machine_parseable"],
                        "eligible": eligible(r), "weight_errors": r.get("weight_errors", []),
                        "raw_sha256": r["raw_sha256"],
                        "canonical_formula_sha256": r.get("canonical_formula_sha256"),
                        "nvars": r.get("nvars"), "nclauses": r.get("nclauses"),
                        "literal_occurrences": r.get("literal_occurrences"),
                        "provenance_tags": sorted(normalized_sources(r))}
                       for r in odd_rows]
    result = {
        "schema": "xai.phase9.cross-split-audit.v1",
        "completed_utc": now_utc(),
        "freeze_file_sha256": sha256_file(freeze_path),
        "frozen_code_revision": freeze["code_revision"],
        "audit_code_revision": git("rev-parse", "HEAD"),
        "archive": freeze["archive"],
        "protocol": "Compare even public vs odd final-test members using exact decompressed bytes SHA-256, the Phase 0 canonical formula SHA-256, normalized exact c r provenance tags, and same-variable distinct-clause Jaccard >= 0.95. Any related eligible odd record is excluded from final-test claims and its full cross-split pair is reported.",
        "public_eligible_count": len(new_eligible),
        "odd_candidate_count": len(odd_rows),
        "odd_eligible_count": len(eligible_odd),
        "cross_split_related_pair_count": len(pair_rows),
        "eligible_odd_excluded_count": len(excluded_odd),
        "eligible_odd_excluded_indices": excluded_odd,
        "eligible_odd_claim_set_count": len(eligible_odd - set(excluded_odd)),
        "source_dependence": "unknown where c r provenance tags are absent",
        "cross_split_pairs": pair_rows,
        "odd_eligibility": odd_eligibility,
        "audit_complete": True,
    }
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({k: result[k] for k in (
        "public_eligible_count", "odd_candidate_count", "odd_eligible_count",
        "cross_split_related_pair_count", "eligible_odd_excluded_count",
        "eligible_odd_claim_set_count", "audit_complete")}, indent=2))
    print(f"audit artifact: {out}")


def proc_group_usage(pgid: int) -> tuple[int, int]:
    total_rss = total_cpu = 0
    ticks = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            stat = (entry / "stat").read_text()
            end = stat.rfind(")")
            fields = stat[end + 2:].split()
            if int(fields[2]) != pgid:
                continue
            total_cpu += int(fields[11]) + int(fields[12])
            status = (entry / "status").read_text()
            match = re.search(r"^VmRSS:\s+(\d+)\s+kB", status, re.MULTILINE)
            if match:
                total_rss += int(match.group(1)) * 1024
        except (OSError, ValueError, IndexError):
            continue
    return total_rss, int(total_cpu * 1000 / ticks)


def run_process(command: list[str], input_path: Path, timeout_sec: int,
                memory_bytes: int, cpu: int, log_dir: Path) -> dict[str, Any]:
    log_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = log_dir / "stdout.txt"
    stderr_path = log_dir / "stderr.txt"
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "LANG": "C", "OMP_NUM_THREADS": "1",
                "OPENBLAS_NUM_THREADS": "1", "MKL_NUM_THREADS": "1"})

    def preexec() -> None:
        import resource
        resource.setrlimit(resource.RLIMIT_CPU, (CPU_BUDGET_SECONDS + 1, CPU_BUDGET_SECONDS + 2))
        if hasattr(os, "sched_setaffinity"):
            os.sched_setaffinity(0, {cpu})

    full_command = [*command, str(input_path)]
    start = time.monotonic()
    peak_rss = 0
    cpu_ms = 0
    sample_count = 0
    timed_out = memory_killed = False
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        process = subprocess.Popen(full_command, cwd=ROOT, env=env, stdout=stdout,
                                  stderr=stderr, start_new_session=True, preexec_fn=preexec)
        pgid = process.pid
        while process.poll() is None:
            rss, cpu_now = proc_group_usage(pgid)
            sample_count += 1
            peak_rss = max(peak_rss, rss)
            cpu_ms = max(cpu_ms, cpu_now)
            elapsed = time.monotonic() - start
            if rss >= memory_bytes:
                memory_killed = True
                os.killpg(pgid, signal.SIGTERM)
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    os.killpg(pgid, signal.SIGKILL)
                break
            if elapsed >= timeout_sec:
                timed_out = True
                os.killpg(pgid, signal.SIGTERM)
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    os.killpg(pgid, signal.SIGKILL)
                break
            time.sleep(0.01)
        returncode = process.wait()
        rss, cpu_now = proc_group_usage(pgid)
        peak_rss = max(peak_rss, rss)
        cpu_ms = max(cpu_ms, cpu_now)
    elapsed_ms = int((time.monotonic() - start) * 1000)
    stdout_bytes, stderr_bytes = stdout_path.read_bytes(), stderr_path.read_bytes()
    result = {
        "command": full_command,
        "returncode": returncode,
        "wall_ms": elapsed_ms,
        "cpu_ms_sampled": cpu_ms if sample_count else None,
        "peak_rss_bytes_sampled": peak_rss if sample_count else None,
        "resource_sample_count": sample_count,
        "wall_timeout": timed_out,
        "memory_limit_killed": memory_killed,
        "stdout_sha256": sha256_bytes(stdout_bytes),
        "stderr_sha256": sha256_bytes(stderr_bytes),
        "stdout": stdout_bytes.decode("utf-8", errors="replace"),
        "stderr": stderr_bytes.decode("utf-8", errors="replace"),
    }
    return result


def classify_result(system: str, proc: dict[str, Any]) -> dict[str, Any]:
    # Output emitted before a timeout, kill, or nonzero exit is never a result.
    if proc["memory_limit_killed"] or proc["wall_timeout"]:
        if proc["memory_limit_killed"]:
            status = "memory_limit"
        else:
            status = "timeout"
        proc.update({"system_status": status, "reported_status": None,
                     "exact_count": None, "solver_sat_label": None})
        return proc
    if proc["returncode"] != 0:
        reported = None
        if system.startswith("xai"):
            match = re.search(r"^c xai status=([^\s]+)", proc["stdout"], re.MULTILINE)
            reported = match.group(1) if match else None
        status = reported if reported in {"parse_error", "unsupported", "resource_limit", "timeout"} else (
            "signal" if proc["returncode"] < 0 else "nonzero_exit")
        proc.update({"system_status": status, "reported_status": reported,
                     "exact_count": None, "solver_sat_label": None})
        return proc
    status = None
    count: Fraction | None = None
    sat_match = re.search(r"^s (SATISFIABLE|UNSATISFIABLE|UNKNOWN)\s*$", proc["stdout"], re.MULTILINE)
    solver_sat_label = sat_match.group(1) if sat_match else None
    reported_status: str | None = None
    output = proc["stdout"]
    if system.startswith("xai"):
        m = re.search(r"^c xai status=([^\s]+)", output, re.MULTILINE)
        reported_status = m.group(1) if m else None
        m = re.search(r"^c s exact rational\s+([^\s]+)", output, re.MULTILINE)
        if m:
            try:
                count = Fraction(m.group(1))
            except (ValueError, ZeroDivisionError):
                status = "unparseable_exact_count"
        if status is None:
            if proc["returncode"] == 0 and reported_status == "solved" and count is not None:
                status = "solved_exact"
            elif reported_status:
                status = reported_status
            elif proc["returncode"] < 0:
                status = "signal"
            else:
                status = "nonzero_exit" if proc["returncode"] else "unrecognized_output"
    else:
        m = re.search(r"^c s exact arb frac\s+([^\s]+)", output, re.MULTILINE)
        if m:
            try:
                count = Fraction(m.group(1))
            except (ValueError, ZeroDivisionError):
                status = "unparseable_exact_count"
        if status is None:
            if proc["returncode"] == 0 and count is not None:
                status = "solved_exact"
            elif proc["returncode"] < 0:
                status = "signal"
            else:
                status = "nonzero_exit" if proc["returncode"] else "unknown_or_unrecognized"
    if status != "solved_exact":
        count = None
    proc.update({"system_status": status, "reported_status": reported_status,
                 "exact_count": f"{count.numerator}/{count.denominator}" if count is not None else None,
                 "solver_sat_label": solver_sat_label})
    return proc


def extract_to_file(archive_path: Path, member_name: str, output: Path) -> tuple[str, int]:
    digest = hashlib.sha256()
    count = 0
    with tarfile.open(archive_path, mode="r:") as archive:
        member = archive.getmember(member_name)
        source = archive.extractfile(member)
        if source is None:
            raise ValueError(f"cannot read archive member {member.name}")
        with lzma.LZMAFile(source, mode="rb") as decoded, output.open("wb") as target:
            while True:
                block = decoded.read(1024 * 1024)
                if not block:
                    break
                count += len(block)
                if count > MAX_RAW_BYTES:
                    raise ValueError(f"decompressed input exceeds cap: {member.name}")
                digest.update(block)
                target.write(block)
    return digest.hexdigest(), count


def bounded_exact_oracle(input_path: Path) -> dict[str, Any]:
    """Independent small-instance exhaustive WMC oracle under the frozen plan cap."""
    started = time.monotonic()
    nvars: int | None = None
    declared_clauses: int | None = None
    weights: dict[int, Fraction] = {}
    clauses: list[list[int]] = []
    pending: list[int] = []
    occurrences = 0
    try:
        with input_path.open("r", encoding="utf-8", errors="strict") as source:
            for lineno, raw in enumerate(source, 1):
                fields = raw.split()
                if not fields:
                    continue
                if fields[0] == "c":
                    if len(fields) >= 2 and fields[1] == "t":
                        if fields != ["c", "t", "wmc"]:
                            return {"system_status": "oracle_unsupported", "exact_count": None,
                                    "reason": f"line {lineno}: unsupported task marker"}
                    elif len(fields) >= 2 and fields[1] == "p":
                        if len(fields) < 3 or fields[2] != "weight":
                            return {"system_status": "oracle_unsupported", "exact_count": None,
                                    "reason": f"line {lineno}: unsupported problem directive"}
                        if len(fields) != 6 or fields[5] != "0":
                            raise ValueError(f"line {lineno}: malformed weight directive")
                        literal = int(fields[3])
                        value = Fraction(fields[4])
                        if literal == 0 or literal in weights:
                            raise ValueError(f"line {lineno}: zero or duplicate weight literal")
                        weights[literal] = value
                    continue
                if fields[0] == "p":
                    if nvars is not None or len(fields) != 4 or fields[1] != "cnf":
                        raise ValueError(f"line {lineno}: malformed or duplicate problem header")
                    nvars, declared_clauses = int(fields[2]), int(fields[3])
                    if nvars < 0 or declared_clauses < 0:
                        raise ValueError(f"line {lineno}: negative problem size")
                    continue
                if nvars is None:
                    raise ValueError(f"line {lineno}: clause before problem header")
                for token in fields:
                    literal = int(token)
                    if literal == 0:
                        clauses.append(pending)
                        pending = []
                    else:
                        if abs(literal) > nvars:
                            raise ValueError(f"line {lineno}: literal outside declared range")
                        pending.append(literal)
                        occurrences += 1
        if nvars is None or declared_clauses is None:
            raise ValueError("missing problem header")
        if pending:
            raise ValueError("unterminated clause")
        if len(clauses) != declared_clauses:
            raise ValueError(f"declared {declared_clauses} clauses, parsed {len(clauses)}")
        if any(abs(lit) > nvars for lit in weights):
            raise ValueError("weight literal outside declared range")
        literal_weights: dict[int, Fraction] = {}
        for variable in range(1, nvars + 1):
            positive, negative = weights.get(variable), weights.get(-variable)
            if positive is None and negative is None:
                positive = negative = Fraction(1)
            elif positive is None or negative is None:
                raise ValueError(f"unpaired weight for variable {variable}")
            if not (Fraction(0) <= positive <= Fraction(1) and
                    Fraction(0) <= negative <= Fraction(1)):
                raise ValueError(f"weight outside [0,1] for variable {variable}")
            if not (positive == 1 and negative == 1) and positive + negative != 1:
                raise ValueError(f"weights do not sum exactly to one for variable {variable}")
            literal_weights[variable], literal_weights[-variable] = positive, negative
    except (OSError, UnicodeDecodeError, ValueError, ZeroDivisionError) as exc:
        return {"system_status": "oracle_parse_error", "exact_count": None,
                "reason": str(exc)}

    workload = (1 << nvars) * occurrences
    if nvars > ORACLE_MAX_VARIABLES or workload > ORACLE_MAX_WORK:
        return {"system_status": "not_eligible_by_oracle_cap", "exact_count": None,
                "nvars": nvars, "literal_occurrences": occurrences,
                "estimated_literal_checks": workload}
    total = Fraction(0)
    satisfying_assignments = 0
    for assignment in range(1 << nvars):
        satisfies = True
        for clause in clauses:
            clause_satisfied = False
            for literal in clause:
                bit = bool(assignment & (1 << (abs(literal) - 1)))
                if (literal > 0 and bit) or (literal < 0 and not bit):
                    clause_satisfied = True
                    break
            if not clause_satisfied:
                satisfies = False
                break
        if not satisfies:
            continue
        satisfying_assignments += 1
        assignment_weight = Fraction(1)
        for variable in range(1, nvars + 1):
            literal = variable if assignment & (1 << (variable - 1)) else -variable
            assignment_weight *= literal_weights[literal]
        total += assignment_weight
    return {"system_status": "solved_exact", "exact_count": f"{total.numerator}/{total.denominator}",
            "nvars": nvars, "literal_occurrences": occurrences,
            "estimated_literal_checks": workload,
            "assignments_enumerated": 1 << nvars,
            "satisfying_assignments": satisfying_assignments,
            "wall_ms": int((time.monotonic() - started) * 1000)}


def run_member(archive_path: Path, member: tarfile.TarInfo, xai: Path, ganak: Path,
               cpu: int, attempt_dir: Path, oracle_eligible: bool) -> dict[str, Any]:
    input_path = attempt_dir / "instance.cnf"
    raw_sha, raw_bytes = extract_to_file(archive_path, member.name, input_path)
    oracle = bounded_exact_oracle(input_path) if oracle_eligible else {
        "system_status": "not_eligible_by_oracle_cap", "exact_count": None}
    results: dict[str, Any] = {}
    ganak_cmd = [str(ganak), *GANAK_OPTIONS]
    results["ganak"] = classify_result("ganak", run_process(
        ganak_cmd, input_path, CPU_BUDGET_SECONDS, MEMORY_BUDGET_BYTES, cpu, attempt_dir / "ganak"))
    comparisons = {}
    oracle_comparisons = {}
    baseline_count = results["ganak"].get("exact_count")
    oracle_count = oracle.get("exact_count") if oracle.get("system_status") == "solved_exact" else None
    mismatch_systems: list[str] = []
    if oracle_count is not None and results["ganak"]["system_status"] == "solved_exact":
        if baseline_count != oracle_count:
            mismatch_systems.append("ganak_vs_independent_oracle")
        oracle_comparisons["ganak"] = {"compared": True, "matches": baseline_count == oracle_count}
    else:
        oracle_comparisons["ganak"] = {"compared": False, "matches": None}
    mismatch = bool(mismatch_systems)
    configs_to_run = [] if mismatch else XAI_CONFIGS
    for position, config in enumerate(configs_to_run):
        name = config["name"]
        cmd = [str(xai), *XAI_BASE_OPTIONS, *config["args"]]
        results[name] = classify_result(name, run_process(
            cmd, input_path, CPU_BUDGET_SECONDS, MEMORY_BUDGET_BYTES, cpu, attempt_dir / name))
        count = results[name].get("exact_count")
        both = results[name]["system_status"] == "solved_exact" and results["ganak"]["system_status"] == "solved_exact"
        matches = bool(both and count == baseline_count)
        comparisons[name] = {"both_completed": both, "exact_counts_equal": matches if both else None,
                             "directly_verified": matches}
        oracle_match = (count == oracle_count) if oracle_count is not None and results[name]["system_status"] == "solved_exact" else None
        oracle_comparisons[name] = {"compared": oracle_match is not None, "matches": oracle_match}
        if oracle_match is False:
            mismatch_systems.append(f"{name}_vs_independent_oracle")
        if both and count != baseline_count:
            mismatch_systems.append(f"{name}_vs_ganak")
        if (both and count != baseline_count) or oracle_match is False:
            mismatch = True
            for unrun in XAI_CONFIGS[position + 1:]:
                results[unrun["name"]] = {
                    "system_status": "not_run_after_mismatch", "exact_count": None,
                    "solver_sat_label": None, "wall_ms": 0, "cpu_ms_sampled": None,
                    "peak_rss_bytes_sampled": None, "resource_sample_count": 0,
                    "wall_timeout": False,
                    "memory_limit_killed": False, "stdout": "", "stderr": "",
                    "stdout_sha256": None, "stderr_sha256": None, "command": []}
                comparisons[unrun["name"]] = {"both_completed": False,
                                                "exact_counts_equal": None,
                                                "directly_verified": False}
                oracle_comparisons[unrun["name"]] = {"compared": False, "matches": None}
            break
    if mismatch and not configs_to_run:
        for config in XAI_CONFIGS:
            name = config["name"]
            results[name] = {"system_status": "not_run_after_mismatch", "exact_count": None,
                             "solver_sat_label": None, "wall_ms": 0, "cpu_ms_sampled": None,
                             "peak_rss_bytes_sampled": None, "resource_sample_count": 0,
                             "wall_timeout": False,
                             "memory_limit_killed": False, "stdout": "", "stderr": "",
                             "stdout_sha256": None, "stderr_sha256": None, "command": []}
            comparisons[name] = {"both_completed": False, "exact_counts_equal": None,
                                 "directly_verified": False}
            oracle_comparisons[name] = {"compared": False, "matches": None}
    return {"member": member.name, "index": int(MEMBER_RE.search(member.name).group(1)),
            "raw_formula_sha256": raw_sha, "decompressed_bytes": raw_bytes,
            "oracle": oracle, "oracle_comparisons": oracle_comparisons,
            "systems": results, "comparisons_to_ganak": comparisons,
            "count_mismatch": mismatch, "mismatch_systems": mismatch_systems}


def load_audit(path: Path, freeze: dict[str, Any], freeze_path: Path) -> dict[str, Any]:
    audit = json.loads(path.read_text(encoding="utf-8"))
    if audit.get("schema") != "xai.phase9.cross-split-audit.v1" or not audit.get("audit_complete"):
        raise ValueError("final split evaluation requires a completed Phase 9 cross-split audit")
    if audit.get("frozen_code_revision") != freeze["code_revision"]:
        raise ValueError("audit does not correspond to this frozen code revision")
    if audit.get("freeze_file_sha256") != sha256_file(freeze_path):
        raise ValueError("audit is tied to a different freeze manifest")
    if audit.get("archive", {}).get("sha256") != freeze["archive"]["sha256"]:
        raise ValueError("audit archive differs from the frozen data snapshot")
    return audit


def run_evaluation(args: argparse.Namespace) -> None:
    archive_path = args.archive.resolve()
    freeze_path = args.freeze.resolve()
    xai, ganak = args.xai.resolve(), args.ganak.resolve()
    freeze = verify_freeze(freeze_path, archive_path, xai, ganak)
    members = list_members(archive_path)
    split_audit = None
    if args.split == "final-odd":
        if not args.audit:
            raise ValueError("final-odd evaluation requires --audit")
        split_audit = load_audit(args.audit.resolve(), freeze, freeze_path)
        excluded = set(split_audit["eligible_odd_excluded_indices"])
        metadata_by_index = {int(r["index"]): r for r in split_audit["odd_eligibility"]}
        eligible_indices = {i for i, r in metadata_by_index.items() if r["eligible"]} - excluded
    else:
        public_audit = json.loads(PUBLIC_AUDIT_DEFAULT.read_text(encoding="utf-8"))
        if public_audit.get("archive_md5") != freeze["archive"]["md5"]:
            raise ValueError("Phase 0 public audit does not match frozen archive")
        metadata_by_index = {int(r["index"]): r for r in public_audit["files"]}
        eligible_indices = {i for i, r in metadata_by_index.items()
                            if r.get("machine_parseable") and not r.get("weight_errors")}
        if len(eligible_indices) != 98:
            raise ValueError("frozen public development eligibility must contain 98 members")
    if args.split == "public-even":
        eligible_indices = {i for i in eligible_indices if i % 2 == 0}
    else:
        eligible_indices = {i for i in eligible_indices if i % 2 == 1}

    claim_denominator = len(eligible_indices)
    # Archive headers are indexed first; only selected split members are decompressed here.
    if args.max_instances is not None:
        if args.split != "public-even":
            raise ValueError("--max-instances is allowed only for public development smoke runs")
        eligible_indices = set(sorted(eligible_indices)[:args.max_instances])
    if not eligible_indices:
        raise ValueError("no eligible members remain in the requested split")
    cpu_list = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else [0]
    cpu = args.cpu if args.cpu is not None else cpu_list[0]
    if cpu not in cpu_list:
        raise ValueError(f"CPU {cpu} is outside the current allowed affinity {cpu_list}")
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    existing: set[int] = set()
    if args.resume and out.exists():
        for line in out.read_text(encoding="utf-8").splitlines():
            if line.strip():
                previous = json.loads(line)
                if previous.get("split") != args.split or previous.get("frozen_code_revision") != freeze["code_revision"]:
                    raise ValueError("cannot resume from rows belonging to another split or frozen revision")
                if previous.get("eligibility_denominator") != claim_denominator:
                    raise ValueError("cannot resume from a different eligible claim-set denominator")
                if previous.get("smoke_run") != (args.max_instances is not None):
                    raise ValueError("cannot mix smoke rows with a full evaluation")
                if previous.get("count_mismatch"):
                    raise ValueError("cannot resume after a count mismatch; the frozen stopping rule halted the run")
                index = int(previous["index"])
                if index in existing:
                    raise ValueError(f"duplicate completed index in resume stream: {index}")
                existing.add(index)
    mode = "a" if args.resume else "w"
    mismatch_found = False
    with out.open(mode, encoding="utf-8") as result_file, tempfile.TemporaryDirectory(prefix="xai-phase9-") as tmp:
        tmp_path = Path(tmp)
        for number, index in enumerate(sorted(eligible_indices), 1):
            if index in existing:
                continue
            member = members[index]
            record_dir = tmp_path / f"{args.split}-{index:03d}"
            record_dir.mkdir()
            meta = metadata_by_index[index]
            nvars = meta.get("nvars")
            occurrences = meta.get("literal_occurrences")
            estimated_work = (1 << int(nvars)) * int(occurrences) if nvars is not None and occurrences is not None else None
            oracle_eligible = (nvars is not None and occurrences is not None and
                               int(nvars) <= ORACLE_MAX_VARIABLES and estimated_work is not None and
                               estimated_work <= ORACLE_MAX_WORK)
            oracle_plan = {"eligible_by_cap": bool(oracle_eligible), "nvars": nvars,
                           "literal_occurrences": occurrences,
                           "estimated_literal_checks": estimated_work,
                           "max_variables": ORACLE_MAX_VARIABLES, "max_work": ORACLE_MAX_WORK}
            outcome = run_member(archive_path, member, xai, ganak, cpu, record_dir,
                                 bool(oracle_eligible))
            outcome["oracle"].update(oracle_plan)
            row = {"schema": "xai.phase9.instance-result.v1", "run_utc": now_utc(),
                   "split": args.split, "index": index,
                   "eligibility_denominator": claim_denominator,
                   "smoke_run": args.max_instances is not None,
                   "formula_metadata": {"nvars": meta.get("nvars"),
                                        "nclauses": meta.get("nclauses"),
                                        "literal_occurrences": meta.get("literal_occurrences")},
                   "frozen_code_revision": freeze["code_revision"],
                   "freeze_file_sha256": sha256_file(freeze_path),
                   "cross_split_audit_sha256": sha256_file(args.audit.resolve()) if args.audit else None,
                   "cpu_affinity": [cpu], "limits": protocol_options()["external_per_process_limits"],
                   **outcome}
            result_file.write(json.dumps(row, sort_keys=True) + "\n")
            result_file.flush()
            verified = sum(1 for value in outcome["comparisons_to_ganak"].values() if value["directly_verified"])
            print(f"{args.split} {number}/{len(eligible_indices)} index={index} verified_configs={verified}/4 mismatch={outcome['count_mismatch']}", flush=True)
            if outcome["count_mismatch"]:
                print("FATAL: direct exact-count mismatch; protocol stopping rule halts this split.", file=sys.stderr)
                mismatch_found = True
                break
    print(f"result stream: {out}")
    if mismatch_found:
        raise RuntimeError("direct exact-count mismatch; split evaluation stopped under frozen protocol")


def run_stress(args: argparse.Namespace) -> None:
    archive_path = args.archive.resolve()
    freeze_path = args.freeze.resolve()
    xai, ganak = args.xai.resolve(), args.ganak.resolve()
    freeze = verify_freeze(freeze_path, archive_path, xai, ganak)
    cpu_list = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else [0]
    cpu = args.cpu if args.cpu is not None else cpu_list[0]
    if cpu not in cpu_list:
        raise ValueError(f"CPU {cpu} is outside the current allowed affinity {cpu_list}")

    fixtures = [
        {"name": "weighted_or", "expected_count": "5/6", "text":
         "c t wmc\np cnf 2 1\nc p weight 1 1/3 0\nc p weight -1 2/3 0\nc p weight 2 3/4 0\nc p weight -2 1/4 0\n1 2 0\n"},
        {"name": "duplicate_clause_invariance", "expected_count": "5/6", "text":
         "c t wmc\np cnf 2 2\nc p weight 1 1/3 0\nc p weight -1 2/3 0\nc p weight 2 3/4 0\nc p weight -2 1/4 0\n1 2 0\n1 2 0\n"},
        {"name": "free_variable_default_weight", "expected_count": "2/3",
         "invariance_group": "variable_renaming", "text":
         "c t wmc\np cnf 2 1\nc p weight 1 1/3 0\nc p weight -1 2/3 0\n1 0\n"},
        {"name": "variable_renaming_invariance", "expected_count": "2/3",
         "invariance_group": "variable_renaming", "text":
         "c t wmc\np cnf 2 1\nc p weight 2 1/3 0\nc p weight -2 2/3 0\n2 0\n"},
        {"name": "omitted_weights_default_to_unweighted_count", "expected_count": "2/1", "text":
         "c t wmc\np cnf 1 0\n"},
        {"name": "contradictory_units_unsat", "expected_count": "0/1", "text":
         "c t wmc\np cnf 1 2\n1 0\n-1 0\n"},
        {"name": "satisfiable_zero_wmc", "expected_count": "0/1", "text":
         "c t wmc\np cnf 1 1\nc p weight 1 0 0\nc p weight -1 1 0\n1 0\n"},
        {"name": "malformed_weight_sum", "expected_xai_status": "unsupported", "text":
         "c t wmc\np cnf 1 0\nc p weight 1 1/5 0\nc p weight -1 3/5 0\n"},
        {"name": "corrupted_clause_count", "expected_xai_status": "parse_error", "text":
         "c t wmc\np cnf 1 2\n1 0\n"},
        {"name": "unsupported_projected_query", "expected_xai_status": "unsupported", "text":
         "c t wmc\np cnf 1 0\nc p show 1 0\n"},
        {"name": "node_limit_exhaustion", "expected_xai_status": "resource_limit", "text":
         "c t wmc\np cnf 3 4\n1 2 3 0\n-1 -2 0\n-1 -3 0\n-2 -3 0\n", "node_limit": 1},
    ]
    checks = []
    with tempfile.TemporaryDirectory(prefix="xai-phase9-stress-") as temp:
        temp_path = Path(temp)
        for fixture in fixtures:
            name = fixture["name"]
            input_path = temp_path / f"{name}.cnf"
            content = fixture["text"].encode("utf-8")
            input_path.write_bytes(content)
            node_limit = fixture.get("node_limit", 10_000_000)
            xai_options = ["--timeout-ms", "600000", "--node-limit", str(node_limit), "--max-depth", "4096"]
            if fixture.get("expected_count") is not None:
                oracle_result = bounded_exact_oracle(input_path)
                xai_result = classify_result("xai_full", run_process(
                    [str(xai), *xai_options], input_path, CPU_BUDGET_SECONDS,
                    MEMORY_BUDGET_BYTES, cpu, temp_path / name / "xai"))
                ganak_result = classify_result("ganak", run_process(
                    [str(ganak), *GANAK_OPTIONS], input_path, CPU_BUDGET_SECONDS,
                    MEMORY_BUDGET_BYTES, cpu, temp_path / name / "ganak"))
                expected = fixture["expected_count"]
                passed = (oracle_result["system_status"] == "solved_exact" and
                          oracle_result["exact_count"] == expected and
                          xai_result["system_status"] == "solved_exact" and
                          ganak_result["system_status"] == "solved_exact" and
                          xai_result["exact_count"] == expected and
                          ganak_result["exact_count"] == expected)
                checks.append({"name": name, "invariance_group": fixture.get("invariance_group"),
                               "fixture_sha256": sha256_bytes(content),
                               "expected_count": expected, "independent_oracle": oracle_result,
                               "xai": xai_result,
                               "ganak": ganak_result,
                               "counts_equal_to_expected": passed,
                               "solver_sat_labels": {"xai": xai_result.get("solver_sat_label"),
                                                     "ganak": ganak_result.get("solver_sat_label")},
                               "sat_labels_used_for_correctness": False})
            else:
                xai_result = classify_result("xai_full", run_process(
                    [str(xai), *xai_options], input_path, CPU_BUDGET_SECONDS,
                    MEMORY_BUDGET_BYTES, cpu, temp_path / name / "xai"))
                expected_status = fixture["expected_xai_status"]
                passed = xai_result["system_status"] == expected_status
                checks.append({"name": name, "fixture_sha256": sha256_bytes(content),
                               "expected_xai_status": expected_status, "xai": xai_result,
                               "status_matches_expectation": passed})
    invariance_checks = []
    groups: dict[str, list[dict[str, Any]]] = {}
    for check in checks:
        if check.get("invariance_group"):
            groups.setdefault(check["invariance_group"], []).append(check)
    for group, members in sorted(groups.items()):
        xai_counts = [item["xai"].get("exact_count") for item in members]
        ganak_counts = [item["ganak"].get("exact_count") for item in members]
        passed = (len(members) >= 2 and all(item["counts_equal_to_expected"] for item in members)
                  and len(set(xai_counts)) == 1 and len(set(ganak_counts)) == 1)
        invariance_checks.append({"group": group, "members": [item["name"] for item in members],
                                  "xai_counts": xai_counts, "ganak_counts": ganak_counts,
                                  "passed": passed})
    result = {"schema": "xai.phase9.stress.v1", "completed_utc": now_utc(),
              "frozen_code_revision": freeze["code_revision"],
              "freeze_file_sha256": sha256_file(freeze_path),
              "claim_scope": "synthetic stress checks only; no benchmark or certificate evidence",
              "passed": (all(c.get("counts_equal_to_expected", c.get("status_matches_expectation", False))
                             for c in checks) and all(item["passed"] for item in invariance_checks)),
              "checks": checks,
              "invariance_checks": invariance_checks,
              "not_applicable": ["entity identity resolution/errors (CNF variables are not entities)",
                                 "temporal/domain shift (no time or outcome labels in the task)",
                                 "human-source dependence (outside solver input contract)",
                                 "calibration, predictive scores, conformal coverage, selective risk"]}
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"passed": result["passed"], "checks": len(checks), "output": str(out)}, indent=2))
    if not result["passed"]:
        raise RuntimeError("one or more synthetic stress checks failed")


def summarize(args: argparse.Namespace) -> None:
    rows = [json.loads(line) for line in args.input.read_text(encoding="utf-8").splitlines() if line.strip()]
    if not rows:
        raise ValueError("no result rows found")
    systems = ["ganak", *(c["name"] for c in XAI_CONFIGS)]
    summary: dict[str, Any] = {"schema": "xai.phase9.summary.v1", "split": rows[0]["split"],
                               "records": len(rows), "expected_records": max(r["eligibility_denominator"] for r in rows),
                               "frozen_code_revision": rows[0]["frozen_code_revision"],
                               "direct_verification_only": True, "systems": {}, "mismatch_indices": []}
    for system in systems:
        statuses = Counter(r["systems"][system]["system_status"] for r in rows)
        attempted = [r["systems"][system] for r in rows
                     if r["systems"][system]["system_status"] != "not_run_after_mismatch"]
        times = sorted(item["wall_ms"] for item in attempted)
        rss_samples = [r["systems"][system].get("peak_rss_bytes_sampled")
                       for r in rows if r["systems"][system].get("peak_rss_bytes_sampled") is not None]
        exact_count = statuses.get("solved_exact", 0)
        summary["systems"][system] = {
            "statuses": dict(sorted(statuses.items())),
            "solved_exact": exact_count,
            "completion_rate_in_evaluated_rows": f"{exact_count}/{len(rows)}",
            "median_wall_ms": statistics.median(times) if times else None,
            "max_wall_ms": max(times) if times else None,
            "max_sampled_rss_bytes": max(rss_samples) if rss_samples else None,
            "resource_sampled_runs": len(rss_samples),
        }
    for config in [c["name"] for c in XAI_CONFIGS]:
        verified = sum(bool(r["comparisons_to_ganak"][config]["directly_verified"]) for r in rows)
        both = sum(bool(r["comparisons_to_ganak"][config]["both_completed"]) for r in rows)
        summary["systems"][config]["directly_verified_against_ganak"] = verified
        summary["systems"][config]["both_completed"] = both
        summary["systems"][config]["exact_match_fraction_among_both_completed"] = (
            f"{verified}/{both}" if both else "not estimable")
    summary["mismatch_indices"] = [r["index"] for r in rows if r["count_mismatch"]]
    oracle_statuses = Counter(r.get("oracle", {}).get("system_status", "missing") for r in rows)
    oracle_pairs_compared = sum(
        bool(value.get("compared"))
        for row in rows for value in row.get("oracle_comparisons", {}).values())
    oracle_pairs_matched = sum(
        bool(value.get("compared") and value.get("matches"))
        for row in rows for value in row.get("oracle_comparisons", {}).values())
    summary["independent_oracle"] = {
        "eligible_by_predeclared_cap": sum(bool(r.get("oracle", {}).get("eligible_by_cap")) for r in rows),
        "statuses": dict(sorted(oracle_statuses.items())),
        "solver_pairs_compared": oracle_pairs_compared,
        "solver_pairs_matched": oracle_pairs_matched,
        "mismatch_indices": [r["index"] for r in rows
                             if any("independent_oracle" in item for item in r.get("mismatch_systems", []))],
    }
    expected = summary["expected_records"]
    all_indices_unique = len({r["index"] for r in rows}) == len(rows)
    any_smoke = any(r.get("smoke_run", False) for r in rows)
    nontrivial_matches = sum(
        bool(r["comparisons_to_ganak"]["xai_full"]["directly_verified"])
        and (r.get("formula_metadata", {}).get("nvars") or 0) >= 2
        and (r.get("formula_metadata", {}).get("nclauses") or 0) >= 1
        for r in rows)
    summary["evaluation_complete"] = len(rows) == expected and all_indices_unique and not any_smoke
    summary["nontrivial_full_solver_matches"] = nontrivial_matches
    summary["phase9_practical_gate"] = bool(summary["evaluation_complete"] and
                                             not summary["mismatch_indices"] and
                                             nontrivial_matches >= 1)
    summary["claim_note"] = "Descriptive finite challenge-set counts only; no population confidence intervals. A verified result means successful directly compared exact rational outputs, not a Phase 8 certificate."
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    freeze = commands.add_parser("freeze", help="create the pre-holdout protocol/code/options freeze")
    freeze.add_argument("--archive", type=Path, default=ARCHIVE_DEFAULT)
    freeze.add_argument("--xai", type=Path, required=True)
    freeze.add_argument("--ganak", type=Path, required=True)
    freeze.add_argument("--ganak-source", type=Path, required=True)
    freeze.add_argument("--ganak-build", type=Path, required=True)
    freeze.add_argument("--output", type=Path, default=FREEZE_DEFAULT)
    freeze.set_defaults(func=make_freeze)
    audit = commands.add_parser("audit", help="after freeze, audit cross-split hashes/source/near-duplicates")
    audit.add_argument("--archive", type=Path, default=ARCHIVE_DEFAULT)
    audit.add_argument("--freeze", type=Path, default=FREEZE_DEFAULT)
    audit.add_argument("--output", type=Path, required=True)
    audit.set_defaults(func=audit_splits)
    run = commands.add_parser("run", help="run Ganak, full XAI solver and three predeclared ablations")
    run.add_argument("--archive", type=Path, default=ARCHIVE_DEFAULT)
    run.add_argument("--freeze", type=Path, default=FREEZE_DEFAULT)
    run.add_argument("--xai", type=Path, required=True)
    run.add_argument("--ganak", type=Path, required=True)
    run.add_argument("--split", choices=["public-even", "final-odd"], required=True)
    run.add_argument("--audit", type=Path)
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--max-instances", type=int, help="public-even smoke test only; never valid as the final result")
    run.add_argument("--resume", action="store_true", help="resume from previously completed instance rows")
    run.add_argument("--cpu", type=int)
    run.set_defaults(func=run_evaluation)
    stress = commands.add_parser("stress", help="run frozen direct-count and fail-closed synthetic stress cases")
    stress.add_argument("--archive", type=Path, default=ARCHIVE_DEFAULT)
    stress.add_argument("--freeze", type=Path, default=FREEZE_DEFAULT)
    stress.add_argument("--xai", type=Path, required=True)
    stress.add_argument("--ganak", type=Path, required=True)
    stress.add_argument("--output", type=Path, required=True)
    stress.add_argument("--cpu", type=int)
    stress.set_defaults(func=run_stress)
    summary = commands.add_parser("summarize", help="summarize a JSONL result stream")
    summary.add_argument("--input", type=Path, required=True)
    summary.add_argument("--output", type=Path, required=True)
    summary.set_defaults(func=summarize)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        args.func(args)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, tarfile.TarError) as exc:
        print(f"phase9_eval: error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
