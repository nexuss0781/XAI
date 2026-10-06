# Test Notes

## Build and run

Requirements: CMake 3.20+, a C++20 compiler, and GMP/GMPXX development headers and libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The CMake build uses `-Wall -Wextra -Werror`; the WMC targets are optimized with `-O3` on GCC/Clang and `/O2` on MSVC. The recorded final run is in [`validation.log`](validation.log). Solver input, statuses, compiler options, and explicit resource limits are described in [`WMC_SOLVER.md`](WMC_SOLVER.md).

To reproduce the public-only benchmark audit, obtain the official CC BY 4.0 archive from the [Zenodo record](https://zenodo.org/records/14249068) and run:

```sh
python3 tools/audit_wmc_archive.py \
  data/raw/mc2024-track2-wmc_competition.tar \
  --output RESULT/wmc-public-audit.json
```

The script verifies the archive checksum and that member indices 0–199 each occur exactly once, parses only even-indexed public Track 2 members, and records odd-indexed member names without reading their bodies. The measured Phase 0 summary is in [`RESULT/Phase-0.md`](../RESULT/Phase-0.md), with per-instance audit fields in [`RESULT/wmc-public-audit.json`](../RESULT/wmc-public-audit.json).

## Coverage

The original six pillar groups check selected finite arithmetic and contract fixtures for evidence updates, a Bayesian model mixture, bounded recombination/change detection, WMC/causal smoke values, proper scores/conformal indexing, and certificate-gated abstention.

The separate WMC suite has eight groups: exact rational parsing; exact WMC and SAT/UNSAT/zero-WMC status; free variables, tautologies, and disconnected components; equivalence across all three solver ablations; strict input and typed-instance validation; node/depth/time limits; fixed exhaustive-oracle formulas; and a 67-case weighted formula grid checked against independent brute-force enumeration. The oracle fixtures are small software checks, not benchmark results.

## Limits

A passing test establishes only that tested code matched the expected answer on those fixtures. It does not establish correctness on arbitrary formulas, benchmark-scale accuracy, empirical speed, superiority over Ganak, dataset representativeness, or the behavior of the broader architecture. No full Track 2 solving experiment, corpus training, calibration study, deployment, or real causal identification is claimed.
