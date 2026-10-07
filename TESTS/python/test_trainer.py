import hashlib
import json
import sys
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import grounding_train as grounding  # noqa: E402
import trainer  # noqa: E402
import trainer_hub  # noqa: E402


def record(record_id, subject, relation, answers, aliases=None):
    return {
        "id": record_id,
        "input": f"{subject} [SEP] {relation}",
        "meta": {"sub_surface": aliases or [], "subj_aliases": []},
        "output": [{"answer": answer, "provenance": []} for answer in answers],
    }


def write_dataset(root, rows):
    data_dir = root / "data"
    data_dir.mkdir()
    train_path = data_dir / grounding.EXPECTED_SPLITS["train"]["filename"]
    raw = "".join(json.dumps(row, ensure_ascii=False) + "\n" for row in rows).encode("utf-8")
    train_path.write_bytes(raw)
    data_manifest = {
        "dataset_id": grounding.DATASET_ID,
        "files": {
            "train": {
                "filename": train_path.name,
                "bytes": len(raw),
                "records": len(rows),
                "sha256": hashlib.sha256(raw).hexdigest(),
            }
        },
    }
    (data_dir / grounding.MANIFEST_FILENAME).write_text(
        json.dumps(data_manifest, sort_keys=True), encoding="utf-8"
    )
    return data_dir, train_path, raw


class FakeHubAPI:
    def __init__(self):
        self.files = {"main": {}}

    def create_branch(self, *, branch, revision, **_kwargs):
        if branch not in self.files:
            self.files[branch] = dict(self.files[revision])

    def upload_file(self, *, path_or_fileobj, path_in_repo, revision, **_kwargs):
        self.files[revision][path_in_repo] = Path(path_or_fileobj).read_bytes()

    def get_paths_info(self, *, paths, revision, **_kwargs):
        return [SimpleNamespace(size=len(self.files[revision][paths[0]]))]


class TrainerEngineTests(unittest.TestCase):
    def test_plan_creates_balanced_disjoint_ranges_and_exact_reconstruction(self):
        rows = [record(str(index), "Ada", "occupation", [f"answer-{index}"]) for index in range(5)]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, _, raw = write_dataset(root, rows)
            run_dir = root / "run"
            manifest = trainer.create_plan(data_dir, run_dir, shard_count=3)

            self.assertEqual(manifest["source"]["records"], 5)
            self.assertEqual(
                [shard["records"] for shard in manifest["shards"]], [2, 2, 1]
            )
            self.assertEqual(
                [(shard["start_record_index"], shard["end_record_index_exclusive"])
                 for shard in manifest["shards"]],
                [(0, 2), (2, 4), (4, 5)],
            )
            reconstructed = b"".join(
                (run_dir / "shards" / shard["filename"]).read_bytes()
                for shard in manifest["shards"]
            )
            self.assertEqual(reconstructed, raw)
            for shard in manifest["shards"]:
                shard_path = run_dir / "shards" / shard["filename"]
                self.assertEqual(shard_path.stat().st_size, shard["bytes"])
                self.assertEqual(hashlib.sha256(shard_path.read_bytes()).hexdigest(), shard["sha256"])

    def test_additive_merge_matches_single_pass_count_index(self):
        rows = [
            record("1", "Ada", "occupation", ["mathematician"], ["Augusta Ada King"]),
            record("2", "Ada", "occupation", ["mathematician", "writer"], ["Augusta Ada King"]),
            record("3", "Ada", "occupation", ["writer"]),
            record("4", "Grace", "occupation", ["computer scientist"]),
            record("5", "Ada", "occupation", ["Writer"]),
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, train_path, raw = write_dataset(root, rows)
            _, data_manifest_sha256 = grounding._load_manifest(data_dir)
            run_dir = root / "run"
            run_manifest = trainer.create_plan(data_dir, run_dir, shard_count=3)

            for shard in run_manifest["shards"]:
                trainer.train_shard(run_dir, shard["shard_index"])

            merged_path = run_dir / "merged.sqlite"
            loaded_indices = []
            released_indices = []

            def load_model(index, _shard):
                loaded_indices.append(index)
                return run_dir / "models" / f"shard-{index:04d}.sqlite"

            def release_model(index, _path):
                released_indices.append(index)

            trainer.merge_shards(
                run_dir,
                merged_path,
                shard_model_loader=load_model,
                shard_model_cleanup=release_model,
            )
            self.assertEqual(loaded_indices, [0, 1, 2])
            self.assertEqual(released_indices, [0, 1, 2])

            serial_path = root / "serial.sqlite"
            serial_metadata = grounding.train_index(
                train_path,
                serial_path,
                expected_sha256=hashlib.sha256(raw).hexdigest(),
                expected_bytes=len(raw),
                expected_records=len(rows),
                dataset_manifest_sha256=data_manifest_sha256,
            )
            merged = grounding._open_model(merged_path)
            serial = grounding._open_model(serial_path)
            try:
                merged_candidates = merged.execute(
                    "SELECT subject_key, relation_key, answer_key, answer_surface, count "
                    "FROM candidates ORDER BY subject_key, relation_key, answer_key"
                ).fetchall()
                serial_candidates = serial.execute(
                    "SELECT subject_key, relation_key, answer_key, answer_surface, count "
                    "FROM candidates ORDER BY subject_key, relation_key, answer_key"
                ).fetchall()
                self.assertEqual(merged_candidates, serial_candidates)

                merged_metadata = {
                    key: json.loads(value)
                    for key, value in merged.execute("SELECT key, value FROM metadata")
                }
                self.assertEqual(merged_metadata["training_records"], serial_metadata["training_records"])
                self.assertEqual(merged_metadata["labeled_training_records"], serial_metadata["labeled_training_records"])
                self.assertEqual(
                    merged_metadata["indexed_subject_relation_answer_pairs"],
                    serial_metadata["indexed_subject_relation_answer_pairs"],
                )
                self.assertEqual(merged_metadata["trainer_merge"]["shard_count"], 3)
                self.assertFalse(merged_metadata["trainer_merge"]["individual_shards_evaluated"])
                prediction_row = record("dev", "Augusta Ada King", "occupation", [])
                self.assertEqual(
                    grounding.predict_candidates(merged, prediction_row),
                    grounding.predict_candidates(serial, prediction_row),
                )
            finally:
                merged.close()
                serial.close()

    def test_worker_refuses_tampered_shard_and_merge_requires_all_workers(self):
        rows = [record(str(index), "Ada", "occupation", ["mathematician"]) for index in range(2)]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, _, _ = write_dataset(root, rows)
            run_dir = root / "run"
            trainer.create_plan(data_dir, run_dir, shard_count=2)
            (run_dir / "shards" / "shard-0000.jsonl").write_text("tampered\n", encoding="utf-8")
            with self.assertRaises(grounding.GroundingDataError):
                trainer.train_shard(run_dir, 0)
            with self.assertRaises(grounding.GroundingDataError):
                trainer.merge_shards(run_dir)

    def test_worker_can_train_from_a_verified_external_shard_path(self):
        rows = [
            record("1", "Ada", "occupation", ["mathematician"]),
            record("2", "Grace", "occupation", ["computer scientist"]),
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, _, _ = write_dataset(root, rows)
            run_dir = root / "run"
            trainer.create_plan(data_dir, run_dir, shard_count=2)
            original_shard = run_dir / "shards" / "shard-0000.jsonl"
            external_shard = root / "hub-cache" / "shard-0000.jsonl"
            external_shard.parent.mkdir()
            external_shard.write_bytes(original_shard.read_bytes())
            original_shard.unlink()

            metadata = trainer.train_shard(run_dir, 0, shard_path=external_shard)
            self.assertEqual(metadata["training_records"], 1)
            self.assertTrue((run_dir / "models" / "shard-0000.sqlite").is_file())

    def test_hub_run_names_and_branches_are_safe_and_unique(self):
        trainer_hub._validate_ids("example/xai-artifacts", "dataset", "123456")
        self.assertEqual(trainer_hub._plan_branch("123456"), "trainer-123456-plan")
        self.assertEqual(
            trainer_hub._worker_branch("123456", 19), "trainer-123456-worker-0019"
        )
        with self.assertRaises(trainer_hub.HubTransferError):
            trainer_hub._validate_ids("example/xai-artifacts", "dataset", "../bad")

    def test_plan_refuses_to_replace_an_existing_run_directory(self):
        rows = [record("1", "Ada", "occupation", ["mathematician"])]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, _, _ = write_dataset(root, rows)
            run_dir = root / "run"
            trainer.create_plan(data_dir, run_dir, shard_count=1)
            with self.assertRaises(grounding.GroundingDataError):
                trainer.create_plan(data_dir, run_dir, shard_count=1)

    def test_plan_rejects_more_records_than_the_dataset_manifest(self):
        rows = [record("1", "Ada", "occupation", ["mathematician"])]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, train_path, _ = write_dataset(root, rows)
            with train_path.open("ab") as stream:
                stream.write(
                    (json.dumps(record("2", "Grace", "occupation", ["scientist"])) + "\n")
                    .encode("utf-8")
                )
            with self.assertRaisesRegex(grounding.GroundingDataError, "more than the declared"):
                trainer.create_plan(data_dir, root / "run", shard_count=1)


class TrainerHubOfflineTests(unittest.TestCase):
    def test_hub_transfer_round_trip_with_synthetic_shards_and_models(self):
        rows = [
            record("1", "Ada", "occupation", ["mathematician"]),
            record("2", "Ada", "occupation", ["writer"]),
            record("3", "Grace", "occupation", ["computer scientist"]),
            record("4", "Ada", "occupation", ["mathematician"]),
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, _, _ = write_dataset(root, rows)
            run_dir = root / "plan"
            trainer.create_plan(data_dir, run_dir, shard_count=2)
            api = FakeHubAPI()

            def fake_download(*, filename, revision, cache_dir=None, **_kwargs):
                content = api.files[revision][filename]
                destination_root = Path(cache_dir) if cache_dir else root / "download-cache"
                destination = destination_root / revision / filename
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(content)
                return str(destination)

            with patch.object(
                trainer_hub, "_hub_clients", return_value=(api, fake_download, "test-token")
            ):
                trainer_hub.upload_plan(
                    Namespace(
                        repo_id="example/xai-artifacts",
                        repo_type="dataset",
                        run_id="42",
                        run_dir=run_dir,
                        base_revision="main",
                    )
                )
                for shard_index in range(2):
                    trainer_hub.train_remote_shard(
                        Namespace(
                            repo_id="example/xai-artifacts",
                            repo_type="dataset",
                            run_id="42",
                            shard_index=shard_index,
                            work_dir=root / f"worker-{shard_index}",
                        )
                    )
                with patch.object(
                    trainer_hub.shutil,
                    "disk_usage",
                    return_value=SimpleNamespace(free=10 * 1024**3),
                ):
                    result = trainer_hub.merge_remote_shards(
                        Namespace(
                            repo_id="example/xai-artifacts",
                            repo_type="dataset",
                            run_id="42",
                            work_dir=root / "merge",
                            output_model=root / "merge" / "merged.sqlite",
                        )
                    )

            plan_branch = "trainer-42-plan"
            merged_remote_path = "runs/42/merged/kilt-trex-slot-filling-merged.sqlite"
            self.assertEqual(result["training_records"], len(rows))
            self.assertFalse(result["individual_shards_evaluated"])
            self.assertIn(merged_remote_path, api.files[plan_branch])
            self.assertEqual(
                hashlib.sha256(api.files[plan_branch][merged_remote_path]).hexdigest(),
                result["model_sha256"],
            )
            for shard_index in range(2):
                self.assertIn(
                    f"runs/42/workers/shard-{shard_index:04d}.sqlite",
                    api.files[f"trainer-42-worker-{shard_index:04d}"],
                )


if __name__ == "__main__":
    unittest.main()
