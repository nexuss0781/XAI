# XAI

A research specification for a candidate modular, non-neural architecture built from explicit mathematical components.

This repository is a research proposal plus deterministic C++20 checks. It does **not** claim general intelligence, novelty, real-dataset performance, or production readiness.

## Project documents

- [Project overview](Project.md)
- [Specification index](SPEC/README.md)
- [Formal research paper](SPEC/RESEARCH_PAPER.md)
- [End-to-end specification](SPEC/END_TO_END_FLOW.md)
- [Test notes and run record](TESTS/README.md)

Each pillar has its own specification under `SPEC/components/`. Mathematical sources are linked in the paper and relevant component documents. Test instructions and outcomes live under `TESTS/`, not `SPEC/`.

## Run the checks

Requirements: GCC or another C++20 compiler.

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Werror \
  TESTS/cpp/pillar_tests.cpp -o /tmp/xai-pillar-tests
/tmp/xai-pillar-tests
```

See [TESTS/README.md](TESTS/README.md) for the checked scope and limitations.

## Current status

The mathematical composition is a research hypothesis assembled from established methods. The current tests cover selected finite examples and invariants; no corpus training, fine-tuning, performance benchmark, or deployment has been performed. A file size of 1 GB is not a dataset sufficiency criterion.