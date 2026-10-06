import importlib.util
import sys
import tempfile
import unittest
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("phase9_eval", ROOT / "tools/phase9_eval.py")
phase9 = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = phase9
assert spec.loader is not None
spec.loader.exec_module(phase9)


class Phase9EvaluationTests(unittest.TestCase):
    def test_predeclared_comparison_modes_are_exact_and_fixed(self):
        options = phase9.protocol_options()
        self.assertEqual(options["ganak"]["binary_args"][:4], ["--mode", "1", "--prob", "0"])
        self.assertIn("--threads", options["ganak"]["binary_args"])
        self.assertEqual(options["xai"]["binary_args"], [
            "--timeout-ms", "600000", "--node-limit", "10000000", "--max-depth", "4096"])
        self.assertEqual([x["name"] for x in phase9.XAI_CONFIGS], [
            "xai_full", "xai_no_unit_propagation", "xai_no_components", "xai_first_branch"])

    def test_xai_exact_result_parser_preserves_fraction(self):
        proc = {"memory_limit_killed": False, "wall_timeout": False,
                "returncode": 0, "stdout": "s SATISFIABLE\nc s exact rational 10/15\nc xai status=solved variables=2\n"}
        parsed = phase9.classify_result("xai_full", proc)
        self.assertEqual(parsed["system_status"], "solved_exact")
        self.assertEqual(parsed["exact_count"], "2/3")
        self.assertEqual(parsed["solver_sat_label"], "SATISFIABLE")

    def test_ganak_zero_wmc_label_does_not_override_exact_zero_count(self):
        proc = {"memory_limit_killed": False, "wall_timeout": False,
                "returncode": 0, "stdout": "s UNSATISFIABLE\nc s exact arb frac 0\n"}
        parsed = phase9.classify_result("ganak", proc)
        self.assertEqual(parsed["system_status"], "solved_exact")
        self.assertEqual(parsed["exact_count"], "0/1")
        self.assertEqual(parsed["solver_sat_label"], "UNSATISFIABLE")

    def test_nonzero_unsupported_exit_is_categorized_and_has_no_count(self):
        proc = {"memory_limit_killed": False, "wall_timeout": False,
                "returncode": 3,
                "stdout": "s UNKNOWN\nc xai status=unsupported reason=projected\nc s exact rational 7/8\n"}
        parsed = phase9.classify_result("xai_full", proc)
        self.assertEqual(parsed["system_status"], "unsupported")
        self.assertIsNone(parsed["exact_count"])

    def test_resource_limit_with_partial_count_is_not_a_result(self):
        proc = {"memory_limit_killed": False, "wall_timeout": False,
                "returncode": 0,
                "stdout": "s UNKNOWN\nc s exact rational 7/8\nc xai status=resource_limit nodes=1\n"}
        parsed = phase9.classify_result("xai_full", proc)
        self.assertEqual(parsed["system_status"], "resource_limit")
        self.assertIsNone(parsed["exact_count"])

    def test_timeout_discards_any_partial_count_and_status_label(self):
        proc = {"memory_limit_killed": False, "wall_timeout": True,
                "returncode": -15, "stdout": "s SATISFIABLE\nc s exact rational 1/2\n"}
        parsed = phase9.classify_result("xai_full", proc)
        self.assertEqual(parsed["system_status"], "timeout")
        self.assertIsNone(parsed["exact_count"])
        self.assertIsNone(parsed["solver_sat_label"])

    def test_memory_limit_discards_any_partial_count_and_status_label(self):
        proc = {"memory_limit_killed": True, "wall_timeout": False,
                "returncode": -9, "stdout": "s SATISFIABLE\nc s exact rational 1/2\n"}
        parsed = phase9.classify_result("xai_full", proc)
        self.assertEqual(parsed["system_status"], "memory_limit")
        self.assertIsNone(parsed["exact_count"])
        self.assertIsNone(parsed["solver_sat_label"])

    def test_near_duplicate_threshold_requires_same_variable_count(self):
        a = {"machine_parseable": True, "nvars": 10, "clause_hashes": set(range(100)),
             "canonical_formula_sha256": "a"}
        b = {"machine_parseable": True, "nvars": 10, "clause_hashes": set(range(99)) | {101},
             "canonical_formula_sha256": "b"}
        similarity = phase9.near_duplicate(a, b)
        self.assertIsNotNone(similarity)
        self.assertGreaterEqual(similarity, 0.95)
        b["nvars"] = 11
        self.assertIsNone(phase9.near_duplicate(a, b))

    def test_normalized_provenance_tags_collapse_whitespace_only(self):
        row = {"provenance": ["  source A   group  ", "", "source B"]}
        self.assertEqual(phase9.normalized_sources(row), {"source A group", "source B"})

    def test_fraction_comparison_is_not_float_based(self):
        huge = Fraction("10000000000000000000000000000000000001/3")
        self.assertEqual(huge, Fraction("10000000000000000000000000000000000001/3"))
        self.assertNotEqual(huge, Fraction("10000000000000000000000000000000000002/3"))

    def test_independent_oracle_counts_exact_weighted_formula(self):
        with tempfile.TemporaryDirectory() as temp:
            formula = Path(temp) / "weighted.cnf"
            formula.write_text(
                "c t wmc\np cnf 2 1\nc p weight 1 1/3 0\nc p weight -1 2/3 0\n"
                "c p weight 2 3/4 0\nc p weight -2 1/4 0\n1 2 0\n")
            result = phase9.bounded_exact_oracle(formula)
        self.assertEqual(result["system_status"], "solved_exact")
        self.assertEqual(result["exact_count"], "5/6")
        self.assertEqual(result["assignments_enumerated"], 4)

    def test_independent_oracle_honors_unweighted_default_and_cap(self):
        with tempfile.TemporaryDirectory() as temp:
            formula = Path(temp) / "default.cnf"
            formula.write_text("c t wmc\np cnf 1 0\n")
            result = phase9.bounded_exact_oracle(formula)
            self.assertEqual(result["exact_count"], "2/1")
            formula.write_text("c t wmc\np cnf 21 0\n")
            capped = phase9.bounded_exact_oracle(formula)
        self.assertEqual(capped["system_status"], "not_eligible_by_oracle_cap")


if __name__ == "__main__":
    unittest.main()
