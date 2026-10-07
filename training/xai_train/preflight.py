"""Read-only gate; it never downloads data, creates model weights, or starts training."""
from __future__ import annotations

import importlib.util
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def assess() -> dict[str, object]:
    dataset = json.loads((ROOT / "training/datasets/umr-v2.json").read_text(encoding="utf-8"))
    blockers: list[str] = []
    warnings: list[str] = []
    if dataset.get("training_status") != "approved":
        blockers.append("UMR graphbank/model-artifact training rights have not been confirmed per license.")
    if not dataset.get("archive_downloaded"):
        warnings.append("UMR data archive has not been acquired; no corpus files are in this repository.")
    blockers.append("UMR has one graph reading per sentence; a multi-reading XAI annotation set or approved protocol is still required.")
    blockers.append("UMR 2.0 is only about 3.1 million tokens; a sufficiently large, permissioned multilingual denoising corpus is not selected.")
    blockers.append("A reviewed UMR-to-XAI graph mapping and byte-exact alignment adapter are not implemented.")
    blockers.append("Development graph recall@3 beam evaluation and frozen final-test evaluation are not yet wired into a full training runner.")
    if importlib.util.find_spec("torch") is None:
        blockers.append("PyTorch is not installed in this environment.")
    if importlib.util.find_spec("sentencepiece") is None:
        blockers.append("SentencePiece is not installed in this environment.")
    disk = shutil.disk_usage(ROOT)
    if disk.free < 10 * 1024**3:
        blockers.append(f"Only {disk.free / 1024**3:.2f} GiB disk space is free; the 10 GiB setup threshold is not met.")
    try:
        import torch
        cuda = bool(torch.cuda.is_available())
    except ImportError:
        cuda = False
    if not cuda:
        blockers.append("No CUDA-capable GPU is visible for the proposed reference training schedule.")
    return {
        "ready_to_start_training": not blockers,
        "training_started": False,
        "selected_dataset": dataset["dataset_id"],
        "blockers": blockers,
        "warnings": warnings,
        "environment": {"pytorch_installed": importlib.util.find_spec("torch") is not None,
                         "sentencepiece_installed": importlib.util.find_spec("sentencepiece") is not None,
                         "cuda_available": cuda, "free_disk_bytes": disk.free},
    }


def main() -> None:
    result = assess()
    print(json.dumps(result, indent=2))
    if not result["ready_to_start_training"]:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
