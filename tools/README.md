# Repository Tools

These scripts are small, purpose-specific utilities. Run a script with `--help` before using optional arguments; the frozen Phase 9 utilities enforce protocol and overwrite safeguards.

- [`audit_wmc_archive.py`](audit_wmc_archive.py) — inspect the public portion of the 2024 MCC Track 2 archive and its input/weight contract.
- [`create_version_manifest.py`](create_version_manifest.py) — write a SHA-256 manifest for tracked source files and explicitly supplied inputs.
- [`phase9_eval.py`](phase9_eval.py) — implement the frozen Phase 9 audit and evaluation workflow. Consult [`../TESTS/PHASE9_PROTOCOL.md`](../TESTS/PHASE9_PROTOCOL.md) and verify the freeze before running it.
- [`phase9_repeat_case.py`](phase9_repeat_case.py) — wrapper for a single bounded public-even repeat; this is not a complete benchmark evaluation.

Examples for discovering supported arguments:

```sh
python3 tools/audit_wmc_archive.py --help
python3 tools/create_version_manifest.py --help
python3 tools/phase9_eval.py --help
python3 tools/phase9_repeat_case.py --help
```

The Phase 10 reproduction harness lives in [`../TESTS/phase10_reproduce.py`](../TESTS/phase10_reproduce.py), alongside test and validation utilities. See [`../TESTS/README.md`](../TESTS/README.md) for its clean-build requirements.
