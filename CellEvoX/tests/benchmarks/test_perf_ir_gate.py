#!/usr/bin/env python3
"""Verdict-logic tests for the CI performance gates.

The instruction gate is the only blocking performance signal, so its verdict
logic and measurement parsing are pinned here with fabricated measurements
rather than left to the ADR's prose. The advisory gate's RSS aggregation is
pinned too, because peak RSS is the one wall-clock-era signal that still blocks.
No Valgrind or compiled probe is needed.

Run directly (no pytest dependency required):
    python3 CellEvoX/tests/benchmarks/test_perf_ir_gate.py
"""

import argparse
import io
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

# Keep the test runnable from any working directory or runner, including pytest
# collecting this file from the repository root.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ci_benchmark_gate
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

    def test_delta_equal_to_allowed_is_accepted(self):
        entry = perf_ir_gate.evaluate("w", side(100), side(103), args(accepted_delta={"w": 0.03}))
        self.assertEqual("OK", entry["status"])
        self.assertIn("accepted", entry)

    def test_delta_equal_to_threshold_is_ok_without_marker(self):
        entry = perf_ir_gate.evaluate("w", side(100), side(102), args())
        self.assertEqual("OK", entry["status"])
        self.assertNotIn("accepted", entry)

    def test_delta_just_above_allowed_is_regressed(self):
        entry = perf_ir_gate.evaluate("w", side(1000), side(1031), args(accepted_delta={"w": 0.03}))
        self.assertEqual("REGRESSED", entry["status"])

    def test_improvement_past_threshold_is_improved_despite_an_accepted_delta(self):
        # The improvement side is judged against --threshold, not the accepted
        # ratio: the exception is headroom for a regression, not a wider band.
        entry = perf_ir_gate.evaluate("w", side(100), side(97), args(accepted_delta={"w": 0.035}))
        self.assertEqual("IMPROVED", entry["status"])


class MeasureSideTest(unittest.TestCase):
    def test_picks_minimum_and_reports_spread(self):
        runs = [{"ir": 102, "checksum": "aa"}, {"ir": 100, "checksum": "aa"}]
        with mock.patch.object(perf_ir_gate, "measure", side_effect=runs):
            result = perf_ir_gate.measure_side("base", Path("probe"), "w", args())
        self.assertEqual(100, result["ir"])
        self.assertEqual([102, 100], result["runs"])
        # Pin the relation itself, not just a literal: the spread must be
        # derived from the recorded runs, so changing either one without the
        # other fails this test.
        self.assertAlmostEqual((max(result["runs"]) - min(result["runs"])) / min(result["runs"]),
                               result["spread"])

    def test_stops_at_the_first_error(self):
        # Short-circuiting keeps a failing gate fast: the remaining reset
        # repeats would each spend the per-run timeout again.
        runs = [{"error": "exit 2"}, {"error": "timeout after 1s"}]
        with mock.patch.object(perf_ir_gate, "measure", side_effect=runs) as measurement:
            result = perf_ir_gate.measure_side("base", Path("probe"), "w", args())
        self.assertEqual("exit 2", result["error"])
        self.assertEqual(1, measurement.call_count)

    def test_parse_args_returns_accepted_deltas(self):
        argv = ["perf_ir_gate.py", "--base-binary", "b", "--head-binary", "h",
                "--accepted-delta", "realistic-2d-450=0.035"]
        with mock.patch.object(sys, "argv", argv):
            args = perf_ir_gate.parse_args()
        self.assertEqual({"realistic-2d-450": 0.035}, args.accepted_delta)

    def test_parse_args_rejects_single_run(self):
        argv = ["perf_ir_gate.py", "--base-binary", "b", "--head-binary", "h", "--runs", "1"]
        # argparse also exits for a bad argument, so assert on the message to
        # prove the --runs guard is what fired.
        with mock.patch.object(sys, "argv", argv), self.assertRaises(SystemExit) as raised, \
                mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
            perf_ir_gate.parse_args()
        self.assertEqual(2, raised.exception.code)
        self.assertIn("--runs must be at least 2", stderr.getvalue())

    def test_parse_args_validates_accepted_delta(self):
        # parse_args must route --accepted-delta through the same validator as
        # a direct parse_accepted_deltas call; a ratio at the threshold is
        # rejected before any measurement runs.
        argv = ["perf_ir_gate.py", "--base-binary", "b", "--head-binary", "h",
                "--accepted-delta", "realistic-2d-450=0.01"]
        with mock.patch.object(sys, "argv", argv), self.assertRaises(SystemExit), \
                mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
            perf_ir_gate.parse_args()
        self.assertIn("must exceed --threshold", stderr.getvalue())


class CallgrindParsingTest(unittest.TestCase):
    def test_summary_line_is_parsed(self):
        self.assertEqual(["123456"],
                         perf_ir_gate.SUMMARY_RE.findall("events: Ir\nsummary: 123456\n"))

    def test_summary_must_start_the_line(self):
        self.assertEqual([], perf_ir_gate.SUMMARY_RE.findall("totals: summary: 9\n"))


class MeasureTest(unittest.TestCase):
    """The blocking verdict depends on every measure() failure path returning an error."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.args = argparse.Namespace(work_dir=self.temp.name, valgrind="valgrind", timeout=5)
        # measure() derives the directory from the workload, side, run index and PID.
        self.out_file = Path(self.temp.name) / f"w-base-1-{os.getpid()}" / "callgrind.out"

    def run_measure(self, fake_run):
        with mock.patch.object(perf_ir_gate.subprocess, "run", side_effect=fake_run):
            return perf_ir_gate.measure("base", Path("probe"), "w", 1, self.args)

    def writing_run(self, text, returncode=0, stdout=""):
        def fake(cmd, **kwargs):
            self.out_file.write_text(text)
            return subprocess.CompletedProcess(cmd, returncode, stdout=stdout, stderr="")
        return fake

    def test_parses_ir_and_checksum_and_removes_work_dir(self):
        result = self.run_measure(self.writing_run(
            "events: Ir\nsummary: 1234\n",
            stdout="RESULT workload=w iterations=1 checksum=0xAb\n"))
        self.assertEqual({"ir": 1234, "checksum": "Ab"}, result)
        self.assertFalse(self.out_file.parent.exists())

    def test_timeout_is_an_error(self):
        def fake(cmd, **kwargs):
            raise subprocess.TimeoutExpired(cmd, 5)
        self.assertEqual({"error": "timeout after 5s"}, self.run_measure(fake))

    def test_non_zero_exit_is_an_error_with_output_tail(self):
        result = self.run_measure(self.writing_run("", returncode=2, stdout="boom\n"))
        self.assertEqual("exit 2: boom", result["error"])

    def test_missing_summary_is_an_error(self):
        result = self.run_measure(self.writing_run("events: Ir\n"))
        self.assertIn("got 0", result["error"])

    def test_multiple_summaries_are_an_error(self):
        result = self.run_measure(self.writing_run("summary: 1\nsummary: 2\n"))
        self.assertIn("got 2", result["error"])

    def test_zero_instructions_is_an_error(self):
        result = self.run_measure(self.writing_run("summary: 0\n", stdout="RESULT checksum=0x1\n"))
        self.assertIn("no instructions", result["error"])

    def test_missing_checksum_line_is_an_error(self):
        result = self.run_measure(self.writing_run("summary: 5\n"))
        self.assertIn("RESULT checksum", result["error"])

    def test_duplicate_checksum_tokens_are_an_error(self):
        # One RESULT line carries exactly one checksum; a second token means the
        # output shape changed, so the gate must not guess which one counts.
        result = self.run_measure(self.writing_run(
            "summary: 5\n", stdout="RESULT checksum=0x1 checksum=0x2\n"))
        self.assertIn("got 0", result["error"])

    def test_non_zero_exit_reports_both_streams(self):
        def fake(cmd, **kwargs):
            return subprocess.CompletedProcess(cmd, 2, stdout="probe error\n",
                                               stderr="loader warning\n")
        error = self.run_measure(fake)["error"]
        self.assertIn("loader warning", error)
        self.assertIn("probe error", error)

    def test_missing_output_is_an_error(self):
        def fake(cmd, **kwargs):
            return subprocess.CompletedProcess(cmd, 0, stdout="RESULT checksum=0x1\n", stderr="")
        self.assertIn("not readable", self.run_measure(fake)["error"])


class AggregateMemoryTest(unittest.TestCase):
    """Peak RSS still blocks, so its REGRESSED > UNKNOWN > OK precedence is pinned."""

    def summary(self, status, delta=0.0, base=1000, head=1000):
        if status == "UNKNOWN":
            return {"status": "UNKNOWN"}
        return {"status": status, "delta": delta,
                "baseline_max_rss_kb": base, "current_max_rss_kb": head}

    def test_no_groups_is_unknown(self):
        self.assertEqual("UNKNOWN", ci_benchmark_gate.aggregate_memory([])["status"])

    def test_regressed_wins_over_ok_and_unknown_in_any_order(self):
        for summaries in ([self.summary("OK"), self.summary("REGRESSED", 0.15)],
                          [self.summary("REGRESSED", 0.15), self.summary("OK")],
                          [self.summary("UNKNOWN"), self.summary("REGRESSED", 0.15)]):
            self.assertEqual("REGRESSED", ci_benchmark_gate.aggregate_memory(summaries)["status"])

    def test_unknown_fills_an_ok_aggregate(self):
        result = ci_benchmark_gate.aggregate_memory([self.summary("OK"), self.summary("UNKNOWN")])
        self.assertEqual("UNKNOWN", result["status"])

    def test_reports_the_largest_delta_and_its_rss(self):
        result = ci_benchmark_gate.aggregate_memory(
            [self.summary("OK", 0.02, 1000, 1020), self.summary("OK", -0.05, 1000, 950)])
        self.assertAlmostEqual(-0.05, result["delta"])
        self.assertEqual(950, result["current_max_rss_kb"])

    def test_reports_numbers_from_the_group_that_set_the_verdict(self):
        result = ci_benchmark_gate.aggregate_memory(
            [self.summary("OK", -0.20, 1000, 800), self.summary("REGRESSED", 0.11, 1000, 1110)])
        self.assertEqual("REGRESSED", result["status"])
        self.assertAlmostEqual(0.11, result["delta"])
        self.assertEqual(1110, result["current_max_rss_kb"])


class HasFailureTest(unittest.TestCase):
    """In advisory mode only lost coverage and peak RSS may block."""

    def summary(self, missing=0, memory_status="OK", regressed=0):
        return {"counts": {"missing": missing, "regressed": regressed},
                "memory": {"status": memory_status}}

    def test_advisory_mode_does_not_block_on_timing(self):
        results = {"bench": {"status": "REGRESSED"}}
        self.assertFalse(ci_benchmark_gate.has_failure(results, self.summary(), False, 0.02,
                                                       advisory_timing=True))

    def test_advisory_mode_blocks_on_rss_regression(self):
        self.assertTrue(ci_benchmark_gate.has_failure({}, self.summary(memory_status="REGRESSED"),
                                                       False, 0.02, advisory_timing=True))

    def test_advisory_mode_blocks_on_missing_benchmark(self):
        self.assertTrue(ci_benchmark_gate.has_failure({}, self.summary(missing=1),
                                                       False, 0.02, advisory_timing=True))

    def test_advisory_mode_blocks_on_unknown_rss(self):
        self.assertTrue(ci_benchmark_gate.has_failure({}, self.summary(memory_status="UNKNOWN"),
                                                       False, 0.02, advisory_timing=True))

    def test_non_advisory_mode_blocks_on_a_counted_regression(self):
        self.assertTrue(ci_benchmark_gate.has_failure({}, self.summary(regressed=1), False, 0.02))

    def test_advisory_mode_does_not_block_on_a_counted_regression(self):
        # --advisory-timing demotes the same counted regression that blocks
        # the default mode; only lost coverage and RSS still block.
        self.assertFalse(ci_benchmark_gate.has_failure({}, self.summary(regressed=1), False, 0.02,
                                                       advisory_timing=True))


class MainTest(unittest.TestCase):
    """main() must fail closed before it measures anything."""

    def run_main(self, *extra):
        argv = ["perf_ir_gate.py", "--base-binary", "b", "--head-binary", "h", *extra]
        with mock.patch.object(sys, "argv", argv), self.assertRaises(SystemExit) as raised:
            perf_ir_gate.main()
        return raised.exception

    def test_empty_workload_list_is_an_error(self):
        # An empty --workloads would measure nothing and then report a pass.
        self.assertIn("No workloads selected", str(self.run_main("--workloads", "")))

    def test_unknown_workload_is_an_error(self):
        self.assertIn("Unknown workload", str(self.run_main("--workloads", "not-a-workload")))

    def test_accepted_delta_for_an_unselected_workload_is_an_error(self):
        # The exception would be printed and recorded as active while never
        # applying, which misreads as headroom in the artifact.
        error = self.run_main("--workloads", "run-5-steps",
                              "--accepted-delta", "realistic-2d-450=0.035")
        self.assertIn("realistic-2d-450", str(error))


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

    def test_rejects_entry_without_a_ratio(self):
        with self.assertRaises(SystemExit):
            perf_ir_gate.parse_accepted_deltas(["realistic-2d-450"], self.parser, 0.02)


if __name__ == "__main__":
    unittest.main()
