#!/usr/bin/env python3
"""Audit only public even-indexed instances in the 2024 MCC Track 2 tar archive."""
from __future__ import annotations

import argparse
import hashlib
import json
import lzma
import re
import sys
import tarfile
from collections import Counter
from decimal import Decimal, InvalidOperation
from fractions import Fraction
from pathlib import Path
from typing import Any, Iterable

EXPECTED_MD5 = "1c7e6279eaf29bf3731cebe07eedf107"
MEMBER_RE = re.compile(r"(?:^|/)mc2024_track2-random_(\d+)\.cnf\.xz$")
MAX_RAW_BYTES = 4 * 1024 * 1024 * 1024
MAX_SOLVER_VARIABLES = 1_000_000
MAX_SOLVER_CLAUSES = 10_000_000
MAX_SOLVER_LITERALS = 100_000_000
MAX_SOLVER_LINE_BYTES = 16 * 1024 * 1024


def as_fraction(text: str) -> Fraction:
    """Parse a finite decimal/scientific or integer fraction exactly."""
    if not text or len(text) > 128:
        raise ValueError("weight token is empty or too long")
    if "/" in text:
        if not re.fullmatch(r"[+-]?\d+/[+-]?\d+", text):
            raise ValueError(f"invalid rational weight {text!r}")
        left, right = text.split("/", 1)
        try:
            return Fraction(int(left), int(right))
        except ZeroDivisionError as exc:
            raise ValueError(f"zero denominator in weight {text!r}") from exc
    if not re.fullmatch(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?", text):
        raise ValueError(f"invalid decimal weight {text!r}")
    exponent_match = re.search(r"[eE]([+-]?\d+)$", text)
    exponent = int(exponent_match.group(1)) if exponent_match else 0
    mantissa = re.split(r"[eE]", text, maxsplit=1)[0].lstrip("+-")
    fractional_digits = len(mantissa.split(".", 1)[1]) if "." in mantissa else 0
    if abs(exponent) > 2000 or abs(fractional_digits - exponent) > 2000:
        raise ValueError("weight exponent/scale exceeds supported bound")
    try:
        value = Decimal(text)
    except InvalidOperation as exc:
        raise ValueError(f"invalid weight {text!r}") from exc
    if not value.is_finite():
        raise ValueError(f"non-finite weight {text!r}")
    return Fraction(value)


def clause_digest(clause: Iterable[int]) -> bytes:
    """Hash a canonical clause using signed, fixed-width variable identifiers."""
    digest = hashlib.blake2b(digest_size=16, person=b"xai-wmc-clause")
    for literal in sorted(set(clause), key=lambda x: (abs(x), x < 0)):
        digest.update(int(literal).to_bytes(8, "big", signed=True))
    digest.update(b"\xff")
    return digest.digest()


def parse_instance(lines: Iterable[str]) -> dict[str, Any]:
    """Parse enough of a WMC file to audit syntax, weights, labels, and provenance."""
    nvars = nclauses = None
    declared_wmc = False
    saw_task_type = False
    weights: dict[int, Fraction] = {}
    clause_hashes: set[bytes] = set()
    pending: list[int] = []
    provenance: list[str] = []
    has_solution = False
    parsed_clause_count = 0
    total_literal_occurrences = 0
    max_line_bytes = 0
    empty_clause_count = 0
    tautological_clause_count = 0
    max_clause_len = 0
    vars_used: set[int] = set()

    def finish_clause() -> None:
        nonlocal pending, parsed_clause_count, empty_clause_count, max_clause_len
        nonlocal tautological_clause_count
        normalized = sorted(set(pending), key=lambda x: (abs(x), x < 0))
        literal_set = set(normalized)
        tautology = any(-literal in literal_set for literal in literal_set)
        if tautology:
            tautological_clause_count += 1
        else:
            clause_hashes.add(clause_digest(normalized))
        parsed_clause_count += 1
        empty_clause_count += int(not normalized)
        max_clause_len = max(max_clause_len, len(normalized))
        pending = []

    for lineno, raw in enumerate(lines, 1):
        max_line_bytes = max(max_line_bytes, len(raw.rstrip("\r\n").encode("utf-8")))
        fields = raw.split()
        if not fields:
            continue
        if fields[0] == "c":
            if len(fields) >= 2 and fields[1] == "t":
                if len(fields) != 3 or fields[2] != "wmc":
                    raise ValueError(f"line {lineno}: unsupported/malformed problem type")
                if saw_task_type and not declared_wmc:
                    raise ValueError(f"line {lineno}: conflicting task type")
                saw_task_type = True
                declared_wmc = True
            if len(fields) >= 2 and fields[1] == "r":
                provenance.append(" ".join(fields[2:]))
            if len(fields) >= 2 and fields[1] == "p":
                if len(fields) < 3 or fields[2] != "weight":
                    raise ValueError(f"line {lineno}: unsupported problem-specific directive")
                if len(fields) != 6 or fields[5] != "0":
                    raise ValueError(f"line {lineno}: malformed weight directive")
                literal = int(fields[3])
                if literal == 0 or literal in weights:
                    raise ValueError(f"line {lineno}: zero/duplicate weight literal")
                weights[literal] = as_fraction(fields[4])
            if len(fields) >= 2 and fields[1] == "s":
                has_solution = True
            continue
        if fields[0] == "p":
            if nvars is not None or len(fields) != 4 or fields[1] != "cnf":
                raise ValueError(f"line {lineno}: malformed/duplicate problem header")
            nvars, nclauses = int(fields[2]), int(fields[3])
            if nvars < 0 or nclauses < 0:
                raise ValueError(f"line {lineno}: negative header count")
            continue
        if nvars is None:
            raise ValueError(f"line {lineno}: clause before problem header")
        for token in fields:
            literal = int(token)
            if literal == 0:
                finish_clause()
            else:
                if abs(literal) > nvars:
                    raise ValueError(f"line {lineno}: literal outside declared range")
                pending.append(literal)
                vars_used.add(abs(literal))
                total_literal_occurrences += 1
    if nvars is None or nclauses is None:
        raise ValueError("missing p cnf header")
    if pending:
        raise ValueError("unterminated clause")
    if parsed_clause_count != nclauses:
        raise ValueError(f"declared {nclauses} clauses, parsed {parsed_clause_count}")

    weight_errors: list[str] = []
    weighted_variables = 0
    explicit_unit_pair_variables = 0
    unspecified_weight_variables = 0
    invalid_weight_variables = 0
    for literal in weights:
        if abs(literal) > nvars:
            weight_errors.append(f"out_of_range_literal:{literal}")
    for variable in range(1, nvars + 1):
        positive, negative = weights.get(variable), weights.get(-variable)
        if positive is None and negative is None:
            unspecified_weight_variables += 1
            continue
        if positive is None or negative is None:
            weight_errors.append(f"unpaired:{variable}")
            invalid_weight_variables += 1
            continue
        if not (Fraction(0) <= positive <= Fraction(1) and
                Fraction(0) <= negative <= Fraction(1)):
            weight_errors.append(f"out_of_range:{variable}")
            invalid_weight_variables += 1
            continue
        if positive == 1 and negative == 1:
            explicit_unit_pair_variables += 1
        elif positive + negative == Fraction(1):
            weighted_variables += 1
        else:
            weight_errors.append(f"not_normalized:{variable}")
            invalid_weight_variables += 1

    canonical = hashlib.sha256()
    canonical.update(f"n={nvars};distinct_m={len(clause_hashes)};".encode())
    for digest in sorted(clause_hashes):
        canonical.update(digest)
    for literal, weight in sorted(weights.items()):
        if weights.get(abs(literal)) == 1 and weights.get(-abs(literal)) == 1:
            continue
        canonical.update(f"{literal}:{weight.numerator}/{weight.denominator};".encode())
    return {
        "nvars": nvars,
        "nclauses": nclauses,
        "parsed_clauses": parsed_clause_count,
        "literal_occurrences": total_literal_occurrences,
        "max_line_bytes": max_line_bytes,
        "distinct_clauses": len(clause_hashes),
        "declared_wmc": declared_wmc,
        "explicit_weight_literals": len(weights),
        "variables_with_explicit_weights": explicit_unit_pair_variables + weighted_variables,
        "weighted_variables": weighted_variables,
        "explicit_unit_pair_variables": explicit_unit_pair_variables,
        "unspecified_weight_variables": unspecified_weight_variables,
        "default_weight_variables": explicit_unit_pair_variables + unspecified_weight_variables,
        "invalid_weight_variables": invalid_weight_variables,
        "zero_weight_literals": sum(value == 0 for value in weights.values()),
        "weight_errors": weight_errors,
        "missing_provenance": not provenance,
        "provenance": provenance,
        "has_solution_label": has_solution,
        "empty_clauses": empty_clause_count,
        "tautological_clauses": tautological_clause_count,
        "max_clause_len": max_clause_len,
        "used_variables": len(vars_used),
        "canonical_formula_sha256": canonical.hexdigest(),
        "clause_hashes": clause_hashes,
    }


def hash_file(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def audit_archive(path: Path) -> dict[str, Any]:
    actual_md5 = hash_file(path)
    if actual_md5 != EXPECTED_MD5:
        raise ValueError(f"archive checksum mismatch: {actual_md5}")
    files: list[dict[str, Any]] = []
    heldout_names: list[str] = []
    all_members = 0
    member_indices: set[int] = set()
    with tarfile.open(path, mode="r:") as archive:
        for member in archive:
            match = MEMBER_RE.search(member.name)
            if not match or not member.isfile():
                continue
            all_members += 1
            index = int(match.group(1))
            if index in member_indices:
                raise ValueError(f"duplicate benchmark instance index: {index}")
            member_indices.add(index)
            if index % 2:
                # Do not read/decompress any held-out member bodies.
                heldout_names.append(member.name)
                continue
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f"unable to read public member {member.name}")
            raw_digest = hashlib.sha256()
            raw_count = 0

            def decoded_lines() -> Iterable[str]:
                nonlocal raw_count
                with lzma.LZMAFile(source) as decompressor:
                    for raw_line in decompressor:
                        raw_count += len(raw_line)
                        if raw_count > MAX_RAW_BYTES:
                            raise ValueError(f"public member exceeds decompression cap: {member.name}")
                        raw_digest.update(raw_line)
                        yield raw_line.decode("utf-8", errors="strict")

            try:
                parsed = parse_instance(decoded_lines())
                parsed.update({
                    "machine_parseable": True,
                    "parse_error": None,
                    "raw_sha256": raw_digest.hexdigest(),
                    "decompressed_bytes": raw_count,
                })
            except (ValueError, UnicodeDecodeError, EOFError, lzma.LZMAError) as exc:
                parsed = {
                    "machine_parseable": False,
                    "parse_error": str(exc),
                    "raw_sha256": raw_digest.hexdigest(),
                    "decompressed_bytes": raw_count,
                    "weight_errors": [],
                    "missing_provenance": True,
                    "has_solution_label": False,
                    "declared_wmc": False,
                }
            parsed.update({
                "index": index,
                "member": member.name,
                "compressed_bytes": member.size,
            })
            files.append(parsed)
            if len(files) % 10 == 0:
                print(f"audited {len(files)} public instances; held-out bodies remain unopened",
                      file=sys.stderr, flush=True)
    expected_indices = set(range(200))
    if member_indices != expected_indices:
        missing = sorted(expected_indices - member_indices)
        unexpected = sorted(member_indices - expected_indices)
        raise ValueError(
            f"archive must contain each benchmark index 0..199 exactly once; "
            f"missing={missing}, unexpected={unexpected}")
    files.sort(key=lambda row: row["index"])
    valid = [row for row in files if row["machine_parseable"]]
    duplicate_buckets: dict[str, list[str]] = {}
    for row in valid:
        duplicate_buckets.setdefault(row["canonical_formula_sha256"], []).append(row["member"])
    duplicate_groups = [group for group in duplicate_buckets.values() if len(group) > 1]
    duplicate_records = sum(len(group) for group in duplicate_groups)

    near_pairs: list[dict[str, Any]] = []
    for position, left in enumerate(valid):
        for right in valid[position + 1:]:
            if left["nvars"] != right["nvars"]:
                continue
            a, b = left["clause_hashes"], right["clause_hashes"]
            smaller, larger = (a, b) if len(a) <= len(b) else (b, a)
            if larger and len(smaller) / len(larger) < 0.95:
                continue
            intersection = len(smaller.intersection(larger))
            union_size = len(a) + len(b) - intersection
            similarity = 1.0 if union_size == 0 else intersection / union_size
            if similarity >= 0.95 and left["canonical_formula_sha256"] != right["canonical_formula_sha256"]:
                near_pairs.append({"left": left["member"], "right": right["member"], "jaccard": similarity})

    origin_counts: Counter[str] = Counter()
    for row in valid:
        for record in row.get("provenance", []):
            fields = record.split()
            if fields:
                origin_counts[fields[0]] += 1

    def numeric_stats(field: str, records: list[dict[str, Any]]) -> dict[str, int]:
        values = sorted(int(row[field]) for row in records)
        if not values:
            return {"count": 0}
        return {
            "count": len(values),
            "min": values[0],
            "median_upper": values[len(values) // 2],
            "max": values[-1],
            "total": sum(values),
        }

    compact_files = []
    for row in files:
        compact_files.append({k: v for k, v in row.items() if k != "clause_hashes"})
    near_participants = {name for pair in near_pairs for name in (pair["left"], pair["right"])}
    cap_exceedances = []
    for row in valid:
        reasons = []
        if row["nvars"] > MAX_SOLVER_VARIABLES:
            reasons.append("variables")
        if row["nclauses"] > MAX_SOLVER_CLAUSES:
            reasons.append("clauses")
        if row["literal_occurrences"] > MAX_SOLVER_LITERALS:
            reasons.append("literal_occurrences")
        if row["max_line_bytes"] > MAX_SOLVER_LINE_BYTES:
            reasons.append("line_bytes")
        if row["decompressed_bytes"] > MAX_RAW_BYTES:
            reasons.append("input_bytes")
        if reasons:
            cap_exceedances.append({"member": row["member"], "limits": reasons})
    return {
        "source_archive": str(path),
        "archive_md5": actual_md5,
        "archive_bytes": path.stat().st_size,
        "candidate_member_count": all_members,
        "public_even_count": len(files),
        "heldout_odd_count": len(heldout_names),
        "heldout_names_only": heldout_names,
        "public_machine_parseable_count": len(valid),
        "public_parse_errors": [{"member": r["member"], "error": r["parse_error"]} for r in files if not r["machine_parseable"]],
        "public_bad_weight_records": [r["member"] for r in valid if r["weight_errors"]],
        "public_missing_provenance_count": sum(bool(r["missing_provenance"]) for r in valid),
        "public_label_marker_count": sum(bool(r["has_solution_label"]) for r in valid),
        "public_declared_wmc_count": sum(bool(r["declared_wmc"]) for r in valid),
        "public_instances_with_explicit_weights": sum(
            r["explicit_weight_literals"] > 0 for r in valid),
        "public_explicit_weight_literal_total": sum(r["explicit_weight_literals"] for r in valid),
        "public_weighted_variable_total": sum(r["weighted_variables"] for r in valid),
        "public_default_weight_variable_total": sum(r["default_weight_variables"] for r in valid),
        "public_explicit_unit_pair_variable_total": sum(
            r["explicit_unit_pair_variables"] for r in valid),
        "public_unspecified_weight_variable_total": sum(
            r["unspecified_weight_variables"] for r in valid),
        "public_zero_weight_literal_total": sum(r["zero_weight_literals"] for r in valid),
        "public_empty_clause_total": sum(r["empty_clauses"] for r in valid),
        "public_tautological_clause_total": sum(r["tautological_clauses"] for r in valid),
        "public_exact_duplicate_groups": duplicate_groups,
        "public_exact_duplicate_record_count": duplicate_records,
        "public_near_duplicate_pair_count_jaccard_ge_0_95": len(near_pairs),
        "public_near_duplicate_participant_count": len(near_participants),
        "public_near_duplicate_pairs": near_pairs,
        "public_solver_default_cap_exceedance_count": len(cap_exceedances),
        "public_solver_default_cap_exceedances": cap_exceedances,
        "source_origin_frequency": dict(origin_counts),
        "variables": numeric_stats("nvars", valid),
        "clauses": numeric_stats("nclauses", valid),
        "literal_occurrences": numeric_stats("literal_occurrences", valid),
        "max_line_bytes": numeric_stats("max_line_bytes", valid),
        "decompressed_bytes": numeric_stats("decompressed_bytes", files),
        "compressed_member_bytes": numeric_stats("compressed_bytes", files),
        "files": compact_files,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = audit_archive(args.archive)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    summary = {key: value for key, value in result.items()
               if key not in {"files", "heldout_names_only", "public_near_duplicate_pairs"}}
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
