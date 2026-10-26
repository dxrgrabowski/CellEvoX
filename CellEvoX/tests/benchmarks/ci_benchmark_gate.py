#!/usr/bin/env python3
"""
ci_benchmark_gate.py - Paired, isolated benchmark gate for CI.

The gate runs base and head binaries in the same CI job, isolates benchmark groups
in separate subprocesses, reports confidence-interval precision, and retries a
group with more samples when the first result is inconclusive.

Measurements are interleaved: every round runs base and head back to back, and
the order flips every round (A/B, B/A, ...) so a slow patch of the shared runner
hits both binaries instead of only the second one. Per benchmark, the round means
are collapsed into a median with a bootstrap interval across rounds.
"""

import argparse
import json
import math
import os
import random
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

from compare_benchmarks import (
    benchmark_values,
    classify,
    fmt_interval,
    inconclusive_regression,
    relative_ci,
)

BOOTSTRAP_RESAMPLES = 2000
BOOTSTRAP_SEED = 20261026
DEFAULT_CONFIDENCE_INTERVAL = 0.95

SCRIPT_DIR = Path(__file__).resolve().parent
CAPTURE_SCRIPT = SCRIPT_DIR / "capture_baseline.py"

GROUP_FILTERS = {
    "quick": ["Simulation: High-Performance Regression"],
    "timing": ["Simulation: 2D vs 3D Time Comparison"],
    "snapshot": ["Population snapshot serialization tradeoff"],
}
GROUP_ALIASES = {
    "all": ["quick", "timing", "snapshot"],
    "extended": ["quick", "timing", "snapshot"],
}


def parse_groups(raw: str) -> List[str]:
    groups: List[str] = []
    for item in (part.strip() for part in raw.split(",")):
        if not item:
            continue
        expanded = GROUP_ALIASES.get(item, [item])
        for group in expanded:
            if group not in GROUP_FILTERS:
                valid = ", ".join(sorted(set(GROUP_FILTERS) | set(GROUP_ALIASES)))
                raise SystemExit(f"Unknown benchmark group '{group}'. Valid groups: {valid}")
            if group not in groups:
                groups.append(group)
    return groups or ["quick"]


def run_capture(binary: str, catch_filter: str, samples: int, out_path: Path,
                warmup_ms: int | None, resamples: int | None,
                confidence_interval: float | None, timeout_s: int) -> Dict:
    cmd = [
        sys.executable,
        str(CAPTURE_SCRIPT),
        "--binary", binary,
        "--out", str(out_path),
        "--tag", catch_filter,
        "--benchmark-samples", str(samples),
        "--timeout", str(timeout_s),
    ]
    if warmup_ms is not None:
        cmd.extend(["--benchmark-warmup-time", str(warmup_ms)])
    if resamples is not None:
        cmd.extend(["--benchmark-resamples", str(resamples)])
    if confidence_interval is not None:
        cmd.extend(["--benchmark-confidence-interval", str(confidence_interval)])

    subprocess.run(cmd, check=True)
    with out_path.open() as f:
        return json.load(f)


def percentile(values: List[float], q: float) -> float:
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * q
    lower = int(math.floor(position))
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] + (ordered[upper] - ordered[lower]) * weight


def bootstrap_interval(values: List[float], confidence_interval: float) -> Tuple[float, float]:
    """Bootstrap the mean over rounds; deterministic so gate output is repeatable."""
    if len(values) < 2:
        return values[0], values[0]
    rng = random.Random(BOOTSTRAP_SEED)
    count = len(values)
    means = [sum(rng.choices(values, k=count)) / count for _ in range(BOOTSTRAP_RESAMPLES)]
    alpha = (1.0 - confidence_interval) / 2.0
    return percentile(means, alpha), percentile(means, 1.0 - alpha)


def aggregate_rounds(runs: List[Dict], confidence_interval: float) -> Dict[str, Dict]:
    """Collapse interleaved rounds of one binary into one estimate per benchmark.

    Catch2 exports only mean +/- CI per run, so the across-round distribution of
    those means is the evidence we have. The point estimate is the median of the
    round means (one stolen-CPU round cannot drag it), and the interval is a
    bootstrap of the mean over rounds, never narrower than the average
    within-round Catch2 precision divided by sqrt(rounds).
    """
    per_name: Dict[str, List[Dict]] = {}
    for run in runs:
        for name, raw in run.get("benchmarks", {}).items():
            per_name.setdefault(name, []).append(benchmark_values(raw))

    aggregated: Dict[str, Dict] = {}
    for name, samples in per_name.items():
        means = [sample["mean_ns"] for sample in samples]
        center = statistics.median(means)
        low, high = bootstrap_interval(means, confidence_interval)
        catch2_half = statistics.median(
            [(sample["high_ns"] - sample["low_ns"]) / 2.0 for sample in samples]
        )
        half = max((high - low) / 2.0, catch2_half / math.sqrt(len(samples)))
        low, high = center - half, center + half
        aggregated[name] = {
            "mean_ns": center,
            "low_ns": low,
            "high_ns": high,
            "relative_ci": relative_ci(center, low, high),
            "rounds": len(samples),
        }
    return aggregated


def aggregate_max_rss(runs: List[Dict]) -> int | None:
    values = [run["max_rss_kb"] for run in runs if run.get("max_rss_kb")]
    return int(statistics.median(values)) if values else None


def evaluate(base: Dict, head: Dict, threshold: float, max_relative_ci: float,
             memory_threshold: float) -> Tuple[Dict, Dict]:
    base_benchmarks = base.get("benchmarks", {})
    head_benchmarks = head.get("benchmarks", {})
    results: Dict[str, Dict] = {}
    counts = {"ok": 0, "improved": 0, "regressed": 0, "noisy": 0,
              "unstable": 0, "new": 0, "missing": 0}

    for name, current_raw in sorted(head_benchmarks.items(), key=lambda x: x[0]):
        if name not in base_benchmarks:
            results[name] = {"status": "NEW"}
            counts["new"] += 1
            continue
        item = classify(base_benchmarks[name], current_raw, threshold, max_relative_ci)
        results[name] = item
        if item["status"] == "REGRESSED":
            counts["regressed"] += 1
        elif item["status"] == "IMPROVED":
            counts["improved"] += 1
        elif item["status"] == "NOISY CI":
            counts["noisy"] += 1
        elif item["status"] == "UNSTABLE CI":
            counts["unstable"] += 1
        else:
            counts["ok"] += 1

    for name in sorted(set(base_benchmarks) - set(head_benchmarks)):
        results[name] = {"status": "MISSING"}
        counts["missing"] += 1

    base_rss = base.get("max_rss_kb")
    head_rss = head.get("max_rss_kb")
    memory = {"status": "UNKNOWN"}
    if base_rss and head_rss:
        delta = (head_rss - base_rss) / base_rss
        memory = {
            "baseline_max_rss_kb": base_rss,
            "current_max_rss_kb": head_rss,
            "delta": delta,
            "status": "REGRESSED" if delta > memory_threshold else "OK",
        }

    return results, {"counts": counts, "memory": memory}


def needs_retry(results: Dict[str, Dict]) -> bool:
    return any(item.get("status") in {"NOISY CI", "UNSTABLE CI"} for item in results.values())


def has_failure(results: Dict[str, Dict], summary: Dict, fail_on_inconclusive: bool, threshold: float) -> bool:
    counts = summary["counts"]
    if counts["regressed"] or counts["missing"]:
        return True
    if summary["memory"].get("status") == "REGRESSED":
        return True
    if fail_on_inconclusive:
        return any(inconclusive_regression(item, threshold) for item in results.values())
    return False

def print_group_report(group: str, samples: int, rounds: int, results: Dict[str, Dict], summary: Dict) -> None:
    print(f"\n=== Benchmark group: {group} ({rounds} interleaved rounds x {samples} samples per binary) ===")
    print(f"{'Benchmark':<55} {'Base mean [CI]':>34} {'Head mean [CI]':>34} {'Change':>10}  Status")
    print("-" * 145)
    for name, item in results.items():
        status = item.get("status", "UNKNOWN")
        if status in {"NEW", "MISSING"}:
            print(f"  {name:<53} {'--':>34} {'--':>34} {'--':>10}  {status}")
            continue
        base_text = fmt_interval(item["base_ms"], item["base_low_ms"], item["base_high_ms"], item["base_relative_ci"])
        head_text = fmt_interval(item["current_ms"], item["current_low_ms"], item["current_high_ms"], item["current_relative_ci"])
        print(f"  {name:<53} {base_text:>34} {head_text:>34} {item['delta']:+10.1%}  {status}")
    print("-" * 145)
    memory = summary["memory"]
    if memory.get("status") != "UNKNOWN":
        print(f"Memory RSS: base {memory['baseline_max_rss_kb'] / 1024:.1f} MB | "
              f"head {memory['current_max_rss_kb'] / 1024:.1f} MB | "
              f"change {memory['delta']:+.1%} | {memory['status']}")
    counts = summary["counts"]
    print("Summary: "
          f"{counts['improved']} improved | {counts['ok']} ok | "
          f"{counts['noisy']} noisy | {counts['unstable']} unstable | "
          f"{counts['regressed']} regressed | {counts['new']} new | {counts['missing']} missing")


def run_group(args, group: str, samples: int) -> Tuple[Dict, Dict]:
    group_dir = Path(args.out_dir) / f"{group}_{samples}"
    group_dir.mkdir(parents=True, exist_ok=True)

    combined_results: Dict[str, Dict] = {}
    combined_counts = {"ok": 0, "improved": 0, "regressed": 0, "noisy": 0,
                       "unstable": 0, "new": 0, "missing": 0}
    memory_summaries = []

    confidence_interval = args.benchmark_confidence_interval or DEFAULT_CONFIDENCE_INTERVAL

    for index, catch_filter in enumerate(GROUP_FILTERS[group], start=1):
        safe_name = f"filter_{index}"
        base_runs: List[Dict] = []
        head_runs: List[Dict] = []
        for round_index in range(1, args.rounds + 1):
            sides = [("base", args.base_binary, base_runs), ("head", args.head_binary, head_runs)]
            if round_index % 2 == 0:
                sides.reverse()
            for side, binary, runs in sides:
                out_json = group_dir / f"{side}_{safe_name}_round{round_index}.json"
                print(f"\nRunning {side} benchmark: group={group}, filter='{catch_filter}', "
                      f"round={round_index}/{args.rounds}, samples={samples}", flush=True)
                runs.append(run_capture(binary, catch_filter, samples, out_json,
                                        args.benchmark_warmup_time, args.benchmark_resamples,
                                        args.benchmark_confidence_interval, args.timeout))
        base = {
            "benchmarks": aggregate_rounds(base_runs, confidence_interval),
            "max_rss_kb": aggregate_max_rss(base_runs),
        }
        head = {
            "benchmarks": aggregate_rounds(head_runs, confidence_interval),
            "max_rss_kb": aggregate_max_rss(head_runs),
        }
        results, summary = evaluate(base, head, args.threshold, args.max_relative_ci,
                                    args.memory_threshold)
        combined_results.update(results)
        for key, value in summary["counts"].items():
            combined_counts[key] += value
        memory_summaries.append(summary["memory"])

    memory_status = "OK"
    memory_delta = 0.0
    base_rss = 0
    head_rss = 0
    for memory in memory_summaries:
        if memory.get("status") == "REGRESSED":
            memory_status = "REGRESSED"
        if memory.get("status") != "UNKNOWN" and abs(memory.get("delta", 0.0)) > abs(memory_delta):
            memory_delta = memory["delta"]
            base_rss = memory["baseline_max_rss_kb"]
            head_rss = memory["current_max_rss_kb"]

    summary = {
        "counts": combined_counts,
        "memory": {
            "status": memory_status if memory_summaries else "UNKNOWN",
            "delta": memory_delta,
            "baseline_max_rss_kb": base_rss,
            "current_max_rss_kb": head_rss,
        },
    }
    return combined_results, summary


def main() -> int:
    parser = argparse.ArgumentParser(description="Run paired CellEvoX benchmark gates for CI")
    parser.add_argument("--base-binary", required=True)
    parser.add_argument("--head-binary", required=True)
    parser.add_argument("--groups", default="quick",
                        help="Comma-separated groups: quick, timing, snapshot, all (default: quick)")
    parser.add_argument("--rounds", type=int, default=5,
                        help="Interleaved base/head rounds per filter; the order flips every round "
                             "(default: 5, use 1 for the old blocked behaviour)")
    parser.add_argument("--initial-samples", type=int, default=6,
                        help="Catch2 samples per binary per round (default: 6)")
    parser.add_argument("--retry-samples", type=int, default=12,
                        help="Catch2 samples per binary per round for one retry when a group is "
                             "inconclusive; 0 disables retry")
    parser.add_argument("--threshold", type=float, default=0.05)
    parser.add_argument("--memory-threshold", type=float, default=0.10)
    parser.add_argument("--max-relative-ci", type=float, default=0.05)
    parser.add_argument("--fail-on-inconclusive", action="store_true")
    parser.add_argument("--benchmark-warmup-time", type=int, default=None)
    parser.add_argument("--benchmark-resamples", type=int, default=None)
    parser.add_argument("--benchmark-confidence-interval", type=float, default=None)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--out-dir", default="/tmp/cellevox_benchmark_gate")
    parser.add_argument("--report-json", default=None)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be at least 1")

    groups = parse_groups(args.groups)
    final_report = {
        "config": {
            "groups": groups,
            "rounds": args.rounds,
            "initial_samples": args.initial_samples,
            "retry_samples": args.retry_samples,
            "threshold": args.threshold,
            "memory_threshold": args.memory_threshold,
            "max_relative_ci": args.max_relative_ci,
            "benchmark_resamples": args.benchmark_resamples,
            "benchmark_confidence_interval": args.benchmark_confidence_interval,
            "fail_on_inconclusive": args.fail_on_inconclusive,
        },
        "groups": {},
    }

    failed = False
    for group in groups:
        samples = args.initial_samples
        results, summary = run_group(args, group, samples)
        print_group_report(group, samples, args.rounds, results, summary)

        if args.retry_samples and args.retry_samples > samples and needs_retry(results):
            print(f"\nGroup '{group}' is inconclusive; retrying with {args.retry_samples} samples per round.")
            samples = args.retry_samples
            results, summary = run_group(args, group, samples)
            print_group_report(group, samples, args.rounds, results, summary)

        final_report["groups"][group] = {
            "rounds": args.rounds,
            "samples": samples,
            "results": results,
            "summary": summary,
        }
        if has_failure(results, summary, args.fail_on_inconclusive, args.threshold):
            failed = True

    if args.report_json:
        report_path = Path(args.report_json)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        with report_path.open("w") as f:
            json.dump(final_report, f, indent=2)

    if failed:
        print("\nCI benchmark gate failed.")
        return 1
    print("\nCI benchmark gate passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
