#!/usr/bin/env python3
"""Verdict-logic tests for perf_ir_gate.py.

The instruction gate is the only blocking performance signal, so its verdict
logic is pinned here with fabricated measurements rather than left to the ADR's
prose. No Valgrind or compiled probe is needed.

Run directly (no pytest dependency required):
    python3 CellEvoX/tests/benchmarks/test_perf_ir_gate.py
"""

import argparse
import unittest

import perf_ir_gate


def side(ir, checksum="aa", spread=0.0):
    return {"ir": ir, "runs": [ir, ir], "spread": spread, "checksums": [checksum]}


def args(threshold=0.02, accepted_delta=None, max_spread=0.02):
    return argparse.Namespace(threshold=threshold, accepted_delta=accepted_delta or {},
                              max_spread=max_spread)


class EvaluateTest(unittest.TestCase):
    def test_identical_counts_pass(self):
        self.assertEqual("OK", perf_ir_gate.evaluate("w", side(100), side(100), args())["status"])

    def test_three_percent_regression_blocks(self):
        self.assertEqual("REGRESSED", perf_ir_gate.evaluate("w", side(100), side(103), args())["status"])

    def test_one_percent_is_within_threshold(self):
        self.assertEqual("OK", perf_ir_gate.evaluate("w", side(100), side(101), args())["status"])

    def test_three_percent_improvement_is_reported(self):
        self.assertEqual("IMPROVED", perf_ir_gate.evaluate("w", side(100), side(97), args())["status"])

    def test_checksum_change_is_semantics_changed(self):
        entry = perf_ir_gate.evaluate("w", side(100, "aa"), side(100, "bb"), args())
        self.assertEqual("SEMANTICS CHANGED", entry["status"])
        self.assertIn("aa", entry["detail"])
        self.assertIn("bb", entry["detail"])

    def test_repeat_checksum_disagreement_is_nondeterministic(self):
        base = {"ir": 100, "runs": [100, 100], "spread": 0.0, "checksums": ["aa", "bb"]}
        self.assertEqual("NON-DETERMINISTIC",
                         perf_ir_gate.evaluate("w", base, side(100), args())["status"])

    def test_repeat_spread_is_nondeterministic(self):
        base = side(100, spread=0.03)
        self.assertEqual("NON-DETERMINISTIC",
                         perf_ir_gate.evaluate("w", base, side(100), args())["status"])

    def test_measurement_error_is_missing(self):
        entry = perf_ir_gate.evaluate("w", {"error": "exit 2: (no output)"}, side(100), args())
        self.assertEqual("MISSING", entry["status"])
        self.assertEqual("exit 2: (no output)", entry["detail"])

    def test_accepted_delta_relaxes_one_workload(self):
        entry = perf_ir_gate.evaluate("w", side(100), side(103), args(accepted_delta={"w": 0.035}))
        self.assertEqual("OK", entry["status"])
        self.assertEqual(0.035, entry["accepted"])

    def test_accepted_delta_too_small_still_blocks(self):
        entry = perf_ir_gate.evaluate("w", side(100), side(103), args(accepted_delta={"w": 0.025}))
        self.assertEqual("REGRESSED", entry["status"])

    def test_no_marker_without_a_named_exception(self):
        self.assertNotIn("accepted", perf_ir_gate.evaluate("w", side(100), side(101), args()))

    def test_exception_for_another_workload_does_not_apply(self):
        entry = perf_ir_gate.evaluate("w", side(100), side(103), args(accepted_delta={"other": 0.05}))
        self.assertEqual("REGRESSED", entry["status"])


class ParseAcceptedDeltasTest(unittest.TestCase):
    def setUp(self):
        self.parser = argparse.ArgumentParser()

    def test_parses_one_entry(self):
        self.assertEqual({"realistic-2d-450": 0.035},
                         perf_ir_gate.parse_accepted_deltas(["realistic-2d-450=0.035"],
                                                            self.parser, 0.02))

    def test_rejects_ratio_at_or_below_threshold(self):
        for ratio in ("0.02", "0.01"):
            with self.assertRaises(SystemExit):
                perf_ir_gate.parse_accepted_deltas([f"realistic-2d-450={ratio}"],
                                                   self.parser, 0.02)

    def test_rejects_unknown_workload(self):
        with self.assertRaises(SystemExit):
            perf_ir_gate.parse_accepted_deltas(["not-a-workload=0.03"], self.parser, 0.02)

    def test_rejects_duplicate_workload(self):
        with self.assertRaises(SystemExit):
            perf_ir_gate.parse_accepted_deltas(["realistic-2d-450=0.03", "realistic-2d-450=0.04"],
                                               self.parser, 0.02)


if __name__ == "__main__":
    unittest.main()
