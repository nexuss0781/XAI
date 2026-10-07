from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from training.xai_train.estimate import estimate


class TrainingEstimateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.reference = json.loads((ROOT / "training/reference.json").read_text(encoding="utf-8"))
        self.dataset = json.loads((ROOT / "training/datasets/umr-v2.json").read_text(encoding="utf-8"))

    def test_nineteen_rank_shares_sum_to_published_corpus_counts(self) -> None:
        result = estimate(self.reference, self.dataset)
        self.assertEqual(len(result["rank_estimates"]), 19)
        for key in ("published_tokens", "published_sentences", "published_concepts"):
            shares = [row["data_share_estimate"][key] for row in result["rank_estimates"]]
            self.assertEqual(sum(shares), self.dataset["published_counts"][key.removeprefix("published_")])
            self.assertLessEqual(max(shares) - min(shares), 1)
        self.assertTrue(all(row["synchronized_pretraining_updates"] == 200000 for row in result["rank_estimates"]))
        self.assertTrue(all(row["synchronized_graph_updates"] == 20000 for row in result["rank_estimates"]))

    def test_target_rate_and_reference_replay_count(self) -> None:
        result = estimate(self.reference, self.dataset, target_hours=24)
        self.assertAlmostEqual(result["required_average_global_updates_per_second_for_target"], 220000 / (24 * 3600))
        self.assertAlmostEqual(result["reference_workload"]["candidate_corpus_passes_if_all_published_tokens_were_used_for_pretraining"],
                               200000 * 8192 / 3104299)
        self.assertFalse(result["data_candidate"]["approved_for_training"])
        self.assertEqual(result["status"], "estimate_only_no_training_started")

    def test_worker_count_does_not_imply_a_wall_clock_target(self) -> None:
        result = estimate(self.reference, self.dataset)
        self.assertEqual(result["world_size"], 19)
        self.assertIsNone(result["target_wall_hours"])
        self.assertIsNone(result["required_average_global_updates_per_second_for_target"])

    def test_measured_stage_rates_produce_synchronized_cluster_eta(self) -> None:
        result = estimate(self.reference, self.dataset, target_hours=24,
                          pretrain_updates_per_second=2,
                          graph_updates_per_second=1)
        times = result["time_estimates"]
        self.assertAlmostEqual(times["pretraining"]["estimated_compute_hours"], 200000 / 2 / 3600)
        self.assertAlmostEqual(times["graph_training"]["estimated_compute_hours"], 20000 / 3600)
        self.assertAlmostEqual(times["combined"]["estimated_wall_hours_including_overhead"], 100000 / 3600 + 20000 / 3600)
        self.assertAlmostEqual(times["combined"]["same_estimated_wall_hours_per_training_rank"],
                               times["combined"]["estimated_wall_hours_including_overhead"])
        self.assertFalse(times["combined"]["meets_target_hours"])

    def test_missing_stage_rate_does_not_claim_end_to_end_eta(self) -> None:
        result = estimate(self.reference, self.dataset, pretrain_updates_per_second=2)
        self.assertIn("pretraining", result["time_estimates"])
        self.assertNotIn("combined", result["time_estimates"])

    def test_invalid_rate_and_overhead_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "greater than zero"):
            estimate(self.reference, self.dataset, graph_updates_per_second=0)
        with self.assertRaisesRegex(ValueError, "overhead_fraction"):
            estimate(self.reference, self.dataset, overhead_fraction=1)


if __name__ == "__main__":
    unittest.main()
