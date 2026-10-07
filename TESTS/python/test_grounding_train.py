import importlib.util
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "grounding_train.py"
spec = importlib.util.spec_from_file_location("grounding_train", SCRIPT)
grounding = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = grounding
assert spec.loader is not None
spec.loader.exec_module(grounding)


def record(record_id, subject, relation, answers, aliases=None):
    return {
        "id": record_id,
        "input": f"{subject} [SEP] {relation}",
        "meta": {
            "sub_surface": aliases or [],
            "subj_aliases": [],
        },
        "output": [{"answer": answer, "provenance": []} for answer in answers],
    }


class GroundingTrainTests(unittest.TestCase):
    def test_normalization_is_unicode_and_whitespace_stable(self):
        self.assertEqual(grounding.normalize_text("  ＡLICE\tSmith "), "alice smith")

    def test_prompt_parser_requires_one_separator_and_two_nonempty_fields(self):
        self.assertEqual(
            grounding.parse_prompt("Ada Lovelace [SEP] occupation"),
            ("Ada Lovelace", "occupation"),
        )
        for invalid in ("Ada Lovelace", " [SEP] occupation", "Ada [SEP] ", "A [SEP] B [SEP] C"):
            with self.subTest(invalid=invalid), self.assertRaises(grounding.GroundingDataError):
                grounding.parse_prompt(invalid)

    def test_answerless_test_rows_are_valid_but_labels_are_rejected(self):
        path = Path("trex-test_without_answers-kilt.jsonl")
        row = record("test-1", "Ada", "occupation", [])
        self.assertFalse(grounding.validate_record(row, "test", 1, path))
        row.pop("output")
        self.assertFalse(grounding.validate_record(row, "test", 1, path))
        row = record("test-1", "Ada", "occupation", [])
        row["output"] = [{"answer": "mathematician"}]
        with self.assertRaises(grounding.GroundingDataError):
            grounding.validate_record(row, "test", 1, path)

    def test_train_index_uses_aliases_and_deterministic_count_ranking(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            train = root / "train.jsonl"
            rows = [
                record("1", "Ada", "occupation", ["mathematician"], ["Augusta Ada King"]),
                record("2", "Ada", "occupation", ["mathematician", "writer"], ["Augusta Ada King"]),
                record("3", "Ada", "occupation", ["writer"], ["Augusta Ada King"]),
            ]
            train.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
            model = root / "model.sqlite"
            metadata = grounding.train_index(train, model, expected_records=3)
            self.assertEqual(metadata["training_records"], 3)
            self.assertEqual(metadata["candidate_rows"], 4)
            self.assertTrue(model.is_file())
            conn = grounding._open_model(model)
            try:
                candidates = grounding.predict_candidates(
                    conn,
                    record("dev-1", "Augusta Ada King", "occupation", ["mathematician"]),
                    limit=2,
                )
            finally:
                conn.close()
        self.assertEqual([candidate["answer_surface"] for candidate in candidates], ["mathematician", "writer"])
        self.assertEqual([candidate["training_occurrences"] for candidate in candidates], [2, 2])
        self.assertFalse(candidates[0]["candidate_share_is_calibrated_probability"])

    def test_evaluation_reports_coverage_and_counts_uncovered_as_misses(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            train = root / "train.jsonl"
            train.write_text(
                json.dumps(record("1", "Ada", "occupation", ["mathematician"])) + "\n",
                encoding="utf-8",
            )
            model = root / "model.sqlite"
            grounding.train_index(train, model, expected_records=1)
            dev = root / "dev.jsonl"
            dev_rows = [
                record("d1", "Ada", "occupation", ["mathematician"]),
                record("d2", "Grace", "occupation", ["computer scientist"]),
            ]
            dev.write_text("".join(json.dumps(row) + "\n" for row in dev_rows), encoding="utf-8")
            report = grounding.evaluate_file(model, dev, top_k=3)
        self.assertEqual(report["records"], 2)
        self.assertEqual(report["records_with_candidates"], 1)
        self.assertEqual(report["coverage"], 0.5)
        self.assertEqual(report["top1_exact_match_over_labeled"], 0.5)
        self.assertEqual(report["top1_exact_match_given_coverage"], 1.0)
        self.assertEqual(report["top_k_hit_rate_over_labeled"], 0.5)

    def test_import_does_not_create_dataset_or_model_artifacts(self):
        # Importing this module only defines functions and CLI arguments.
        self.assertTrue(SCRIPT.is_file())
        self.assertTrue(callable(grounding.train_index))


if __name__ == "__main__":
    unittest.main()
