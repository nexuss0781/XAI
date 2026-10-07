#!/usr/bin/env python3
"""Run one bounded public-even reproducibility case using the frozen Phase 9 runner.

This wrapper only selects a single even-indexed, publicly eligible formula. Freeze
verification, formula extraction, process limits, solver commands, output
classification, exact-count comparisons, and result-row construction use the
unchanged tools/phase9_eval.py implementation. The result is a smoke/repeat row,
not a completed or confirmatory Phase 9 evaluation.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import phase9_eval as evaluator  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", type=int, default=8,
                        help="one public-even member index (default: 8)")
    parser.add_argument("--archive", type=Path, default=evaluator.ARCHIVE_DEFAULT)
    parser.add_argument("--freeze", type=Path, default=evaluator.FREEZE_DEFAULT)
    parser.add_argument("--xai", type=Path,
                        default=Path("/workspace/XAI-build-phase9-final/xai_wmc_solver"))
    parser.add_argument("--ganak", type=Path,
                        default=Path("/workspace/XAI/data/vendor/ganak-build/ganak"))
    parser.add_argument("--audit", type=Path,
                        default=ROOT / "RESULT/Phase-9-cross-split-audit.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.index < 0 or args.index % 2:
        parser.error("--index must be a non-negative even public index")
    output = args.output.resolve()
    if output.exists():
        parser.error(f"refusing to overwrite existing result: {output}")

    archive, freeze_path = args.archive.resolve(), args.freeze.resolve()
    xai, ganak = args.xai.resolve(), args.ganak.resolve()
    audit_path = args.audit.resolve()
    freeze = evaluator.verify_freeze(freeze_path, archive, xai, ganak)
    split_audit = evaluator.load_audit(audit_path, freeze, freeze_path)
    if split_audit.get("eligible_odd_claim_set_count") != 95:
        raise ValueError("cross-split audit does not match the frozen Phase 9 report")

    public_audit = json.loads(evaluator.PUBLIC_AUDIT_DEFAULT.read_text(encoding="utf-8"))
    if public_audit.get("archive_md5") != freeze["archive"]["md5"]:
        raise ValueError("Phase 0 public audit does not match the frozen archive")
    metadata_by_index = {int(row["index"]): row for row in public_audit["files"]}
    meta = metadata_by_index.get(args.index)
    if (meta is None or args.index % 2 or not meta.get("machine_parseable")
            or meta.get("weight_errors")):
        raise ValueError(f"index {args.index} is not an eligible public-even record")

    cpu_list = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else [0]
    cpu = cpu_list[0]
    members = evaluator.list_members(archive)
    member = members[args.index]
    oracle_plan = evaluator.oracle_work_plan(meta.get("nvars"), meta.get("literal_occurrences"))
    oracle_plan.update({"max_variables": evaluator.ORACLE_MAX_VARIABLES,
                        "max_work": evaluator.ORACLE_MAX_WORK})

    with tempfile.TemporaryDirectory(prefix="xai-phase9-repeat-") as temporary:
        attempt_dir = Path(temporary) / f"public-even-{args.index:03d}"
        attempt_dir.mkdir()
        outcome = evaluator.run_member(archive, member, xai, ganak, cpu, attempt_dir,
                                       bool(oracle_plan["eligible_by_cap"]))
    outcome["oracle"].update(oracle_plan)
    row = {
        "schema": "xai.phase9.instance-result.v1",
        "run_utc": evaluator.now_utc(),
        "split": "public-even",
        "index": args.index,
        "eligibility_denominator": 98,
        "smoke_run": True,
        "formula_metadata": {"nvars": meta.get("nvars"), "nclauses": meta.get("nclauses"),
                             "literal_occurrences": meta.get("literal_occurrences")},
        "frozen_code_revision": freeze["code_revision"],
        "freeze_file_sha256": evaluator.sha256_file(freeze_path),
        "cross_split_audit_sha256": evaluator.sha256_file(audit_path),
        "cpu_affinity": [cpu],
        "limits": evaluator.protocol_options()["external_per_process_limits"],
        **outcome,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(row, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "index": args.index,
                      "raw_formula_sha256": row["raw_formula_sha256"],
                      "count_mismatch": row["count_mismatch"],
                      "statuses": {name: item["system_status"]
                                   for name, item in row["systems"].items()}}, indent=2))
    if row["count_mismatch"]:
        raise RuntimeError("direct exact-count mismatch; protocol stopping rule halts this repeat")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
