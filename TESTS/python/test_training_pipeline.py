from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from training.xai_train.objectives import accepted_reading_weights, learning_rate, span_corruption
from training.xai_train.prepare import prepare
from training.xai_train.records import validate_record


def make_record(index: int, language: str = "en", source_id: str | None = None) -> dict:
    text = f"text {language} {index}"
    return {
        "record_id": f"r-{index}",
        "source_id": source_id or f"doc-{index}",
        "language": language,
        "text": text,
        "text_origin": "natural",
        "license_id": "test-permission",
        "rights_status": "approved",
        "use_pretraining": True,
        "use_graph_training": True,
        "accepted_readings": [{
            "speech_act": "assertion",
            "nodes": [{"id": "n0", "type": "event", "label": "text", "source_spans": [{"begin": 0, "end": 4}], "implicit": False, "attributes": {}}],
            "edges": [],
        }],
    }


class RecordValidationTests(unittest.TestCase):
    def test_multibyte_utf8_byte_span_is_valid(self) -> None:
        record = make_record(1)
        record["text"] = "café"
        record["accepted_readings"][0]["nodes"][0]["source_spans"] = [{"begin": 0, "end": 5}]
        self.assertEqual(validate_record(record)["text"], "café")

    def test_span_inside_multibyte_scalar_is_rejected(self) -> None:
        record = make_record(2)
        record["text"] = "café"
        record["accepted_readings"][0]["nodes"][0]["source_spans"] = [{"begin": 0, "end": 4}]
        with self.assertRaisesRegex(ValueError, "splits a UTF-8 scalar"):
            validate_record(record)

    def test_source_less_node_must_be_explicitly_implicit(self) -> None:
        record = make_record(3)
        node = record["accepted_readings"][0]["nodes"][0]
        node["source_spans"] = []
        node["implicit"] = False
        with self.assertRaisesRegex(ValueError, "exact source span or be explicitly implicit"):
            validate_record(record)

    def test_synthetic_origin_is_rejected(self) -> None:
        record = make_record(4)
        record["text_origin"] = "template_generated"
        with self.assertRaisesRegex(ValueError, "origins are not allowed"):
            validate_record(record)

    def test_graph_references_must_be_local(self) -> None:
        record = make_record(5)
        record["accepted_readings"][0]["edges"] = [{"source": "n0", "target": "missing", "relation": "ARG0"}]
        with self.assertRaisesRegex(ValueError, "edge endpoints"):
            validate_record(record)


class PreparationTests(unittest.TestCase):
    def test_target_language_and_dev_test_text_are_isolated(self) -> None:
        records = [make_record(i, "en" if i < 60 else "xx") for i in range(120)]
        with tempfile.TemporaryDirectory() as directory:
            result = prepare(records, Path(directory), seed=20261008, target_language="xx")
            self.assertNotIn("xx", result["graph_train_languages"])
            self.assertNotIn("xx", result["graph_dev_languages"])
            self.assertEqual(result["graph_test_languages"], ["xx"])
            pretrain = [json.loads(line) for line in (Path(directory) / "pretrain_train.jsonl").read_text().splitlines()]
            self.assertTrue(pretrain)
            self.assertTrue(all(not row["accepted_readings"] and not row["use_graph_training"] for row in pretrain))
            assignments = result["group_assignments"]
            self.assertTrue(set(assignments.values()) <= {"train", "dev", "test"})

    def test_declared_duplicate_group_stays_in_one_partition(self) -> None:
        records = [make_record(i) for i in range(120)]
        records[0]["duplicate_group"] = "same-source"
        records[1]["duplicate_group"] = "same-source"
        with tempfile.TemporaryDirectory() as directory:
            result = prepare(records, Path(directory), seed=111)
            self.assertEqual(result["group_assignments"]["doc-0"], result["group_assignments"]["doc-1"])

    def test_rights_pending_blocks_preparation(self) -> None:
        records = [make_record(i) for i in range(40)]
        records[0]["rights_status"] = "review_required"
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "rights are not approved"):
                prepare(records, Path(directory), seed=5)


class ObjectiveHelperTests(unittest.TestCase):
    def test_accepted_reading_weights_are_language_document_reading_balanced(self) -> None:
        a = make_record(1, "en")
        a["accepted_readings"].append(a["accepted_readings"][0])
        b = make_record(2, "fr")
        weights = accepted_reading_weights([a, b])
        self.assertEqual(len(weights), 3)
        self.assertAlmostEqual(sum(weights), 1.0)
        self.assertAlmostEqual(weights[0], 0.25)
        self.assertAlmostEqual(weights[1], 0.25)
        self.assertAlmostEqual(weights[2], 0.5)

    def test_span_corruption_is_seed_reproducible_and_preserves_unmasked_content(self) -> None:
        import random
        ids = list(range(1, 101))
        source1, target1 = span_corruption(ids, 999, [1000, 1001, 1002], 2, random.Random(3))
        source2, target2 = span_corruption(ids, 999, [1000, 1001, 1002], 2, random.Random(3))
        self.assertEqual((source1, target1), (source2, target2))
        self.assertTrue(target1)
        self.assertIn(2, target1)

    def test_learning_rate_warms_then_decays(self) -> None:
        values = [learning_rate(i, 100, 1e-3) for i in range(100)]
        self.assertGreater(values[0], 0)
        self.assertAlmostEqual(values[0], 1e-3 / 2)
        self.assertGreater(values[2], values[20])
        self.assertGreater(values[20], values[-1])
        self.assertAlmostEqual(values[-1], 0.0)


if __name__ == "__main__":
    unittest.main()
