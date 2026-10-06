# Test Notes

## Build and run

Requirements: CMake 3.20+, a C++20 compiler, and GMP/GMPXX development headers and libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake applies `-Wall -Wextra -Werror` (or `/W4 /WX` on MSVC); WMC targets additionally use `-O3` on GCC/Clang and `/O2` on MSVC. The tests build from a fresh directory with no generated files required from an earlier run. The recorded Phase 0 run is in [`validation.log`](validation.log); Phase 1's clean-build evidence is in [`RESULT/Phase-1.md`](../RESULT/Phase-1.md). Solver input, statuses, compiler options, and resource limits are described in [`WMC_SOLVER.md`](WMC_SOLVER.md).

## Version manifest

From the repository root, run:

```sh
python3 tools/create_version_manifest.py \
  --output build/version-manifest.json \
  --data data/raw/benchmark.tar \
  --config config/evaluation.json
```

Repeat `--data` and `--config` for each file that affects a run; omit either option when that category is not applicable. The script records the current Git revision, whether tracked files differ from that revision, SHA-256 and byte size for every Git-tracked source/document file, plus hashes and sizes for explicitly supplied data/config files. File contents are never copied into the manifest. Ignored or untracked files are not silently included in the tracked-source set; pass all run-relevant data and configuration explicitly. Keep the manifest with the run report or build artifact, not in a directory exposed to unauthorized readers if file names themselves are sensitive.

## Phase 0 archive audit

To reproduce the public-only benchmark audit, obtain the official CC BY 4.0 archive from the [Zenodo record](https://zenodo.org/records/14249068) and run:

```sh
python3 tools/audit_wmc_archive.py \
  data/raw/mc2024-track2-wmc_competition.tar \
  --output RESULT/wmc-public-audit.json
```

The auditor verifies the archive checksum and that member indices 0–199 each occur exactly once, parses only even-indexed public Track 2 members, and records odd-indexed member names without reading their bodies. The measured Phase 0 summary is in [`RESULT/Phase-0.md`](../RESULT/Phase-0.md), with per-instance audit fields in [`RESULT/wmc-public-audit.json`](../RESULT/wmc-public-audit.json).

## Coverage

The shared-contract suite has four focused groups: typed identifiers and partition invariants; malformed records and explicit failure behavior; canonical JSON and schema-mismatch checks; and the complete machine-readable status catalog, including round trips that prove failures are not silently converted to success. The parser rejects duplicate keys, unknown/missing fields, noncanonical JSON, invalid identifiers, and floating-point JSON numbers.

The original six pillar groups check selected finite arithmetic and contract fixtures for evidence updates, a Bayesian model mixture, bounded recombination/change detection, WMC/causal smoke values, proper scores/conformal indexing, and certificate-gated abstention.

The separate WMC suite has eight groups: exact rational parsing; exact WMC and SAT/UNSAT/zero-WMC status; free variables, tautologies, and disconnected components; equivalence across all three solver ablations; strict input and typed-instance validation; node/depth/time limits; fixed exhaustive-oracle formulas; and a 67-case weighted formula grid checked against independent brute-force enumeration. The oracle fixtures are small software checks, not benchmark results.

## Limits

A passing test establishes only that tested code matched the expected answer on those fixtures. The contract implementation is a lightweight in-process type/serialization layer; it is not persistent storage, an authentication mechanism, or a cross-process service. No test establishes correctness on arbitrary formulas, benchmark-scale accuracy, empirical speed, superiority over Ganak, dataset representativeness, or the behavior of the broader architecture. No full Track 2 solving experiment, corpus training, calibration study, deployment, or real causal identification is claimed.
