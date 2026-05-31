#!/usr/bin/env python3
"""
ci_benchmark_gate.py - Paired, isolated benchmark gate for CI.

The gate runs base and head binaries in the same CI job, isolates benchmark groups
in separate subprocesses, reports confidence-interval precision, and retries a
group with more samples when the first result is inconclusive.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Dict, Iterable, List, Tuple

from compare_benchmarks import benchmark_values, classify, fmt_interval, inconclusive_regression

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

def print_group_report(group: str, samples: int, results: Dict[str, Dict], summary: Dict) -> None:
    print(f"\n=== Benchmark group: {group} ({samples} samples) ===")
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

    for index, catch_filter in enumerate(GROUP_FILTERS[group], start=1):
        safe_name = f"filter_{index}"
        base_json = group_dir / f"base_{safe_name}.json"
        head_json = group_dir / f"head_{safe_name}.json"
        print(f"\nRunning base benchmark: group={group}, filter='{catch_filter}', samples={samples}", flush=True)
        base = run_capture(args.base_binary, catch_filter, samples, base_json,
                           args.benchmark_warmup_time, args.benchmark_resamples,
                           args.benchmark_confidence_interval, args.timeout)
        print(f"Running head benchmark: group={group}, filter='{catch_filter}', samples={samples}", flush=True)
        head = run_capture(args.head_binary, catch_filter, samples, head_json,
                           args.benchmark_warmup_time, args.benchmark_resamples,
                           args.benchmark_confidence_interval, args.timeout)
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
    parser.add_argument("--initial-samples", type=int, default=30)
    parser.add_argument("--retry-samples", type=int, default=60,
                        help="Samples used for one retry when a group is inconclusive; 0 disables retry")
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

    groups = parse_groups(args.groups)
    final_report = {
        "config": {
            "groups": groups,
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
        print_group_report(group, samples, results, summary)

        if args.retry_samples and args.retry_samples > samples and needs_retry(results):
            print(f"\nGroup '{group}' is inconclusive; retrying with {args.retry_samples} samples.")
            samples = args.retry_samples
            results, summary = run_group(args, group, samples)
            print_group_report(group, samples, results, summary)

        final_report["groups"][group] = {
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
