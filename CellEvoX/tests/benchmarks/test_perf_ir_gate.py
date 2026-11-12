#!/usr/bin/env python3
"""Verdict-logic tests for perf_ir_gate.py.

The instruction gate is the only blocking performance signal, so its verdict
logic is pinned here with fabricated measurements rather than left to the ADR's
prose. No Valgrind or compiled probe is needed.

Run directly (no pytest dependency required):
    python3 CellEvoX/tests/benchmarks/test_perf_ir_gate.py
"""

import argparse
import os
import sys
import unittest
from unittest import mock
from pathlib import Path

# Keep the test runnable from any working directory or runner, including pytest
# collecting this file from the repository root.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import perf_ir_gate


def side(ir, checksum="aa", spread=0.0):
    return {"ir": ir, "runs": [ir, ir], "spread": spread, "checksums": [checksum]}


def args(threshold=0.02, accepted_delta=None, max_spread=0.02, runs=2):
    return argparse.Namespace(threshold=threshold, accepted_delta=accepted_delta or {},
                              max_spread=max_spread, runs=runs)


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
        self.assertAlmostEqual(0.03, entry["accepted"]["delta"])
        self.assertEqual(0.035, entry["accepted"]["allowed"])

    def test_accepted_delta_too_small_still_blocks(self):
        entry = perf_ir_gate.evaluate("w", side(100), side(103), args(accepted_delta={"w": 0.025}))
        self.assertEqual("REGRESSED", entry["status"])

    def test_no_marker_without_a_named_exception(self):
        self.assertNotIn("accepted", perf_ir_gate.evaluate("w", side(100), side(101), args()))

    def test_exception_for_another_workload_does_not_apply(self):
        entry = perf_ir_gate.evaluate("w", side(100), side(103), args(accepted_delta={"other": 0.05}))
        self.assertEqual("REGRESSED", entry["status"])


class MeasureSideTest(unittest.TestCase):
    def test_picks_minimum_and_reports_spread(self):
        runs = [{"ir": 102, "checksum": "aa"}, {"ir": 100, "checksum": "aa"}]
        with mock.patch.object(perf_ir_gate, "measure", side_effect=runs):
            result = perf_ir_gate.measure_side("base", Path("probe"), "w", args())
        self.assertEqual(100, result["ir"])
        self.assertEqual([102, 100], result["runs"])
        self.assertAlmostEqual(0.02, result["spread"])

    def test_joins_run_errors(self):
        runs = [{"error": "exit 2"}, {"error": "timeout after 1s"}]
        with mock.patch.object(perf_ir_gate, "measure", side_effect=runs):
            result = perf_ir_gate.measure_side("base", Path("probe"), "w", args())
        self.assertEqual("exit 2; timeout after 1s", result["error"])

    def test_parse_args_rejects_single_run(self):
        argv = ["perf_ir_gate.py", "--base-binary", "b", "--head-binary", "h", "--runs", "1"]
        with mock.patch.object(sys, "argv", argv), self.assertRaises(SystemExit):
            perf_ir_gate.parse_args()


class CallgrindParsingTest(unittest.TestCase):
    def test_summary_line_is_parsed(self):
        self.assertEqual(["123456"],
                         perf_ir_gate.SUMMARY_RE.findall("events: Ir\nsummary: 123456\n"))

    def test_summary_must_start_the_line(self):
        self.assertEqual([], perf_ir_gate.SUMMARY_RE.findall("totals: summary: 9\n"))


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
