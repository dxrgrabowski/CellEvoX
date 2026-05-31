#!/usr/bin/env python3
"""
compare_benchmarks.py - Compare current benchmark results against a stored baseline.

This script treats benchmark timings as intervals, not just scalar means. A speed
regression is clear only when the current mean is slower than the configured
threshold and the current confidence interval is fully above the baseline
confidence interval. Wide confidence intervals are reported as inconclusive.
"""

import argparse
import json
import os
import resource
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

DEFAULT_BINARY = os.path.join(os.path.dirname(__file__), "..", "..", "build", "bin", "CellEvoXTests")
DEFAULT_BASELINE = os.path.join(os.path.dirname(__file__), "baseline.json")
DEFAULT_THRESHOLD = 0.30


def parse_xml_benchmarks(xml_path: str) -> dict:
    tree = ET.parse(xml_path)
    root = tree.getroot()
    results = {}
    for br in root.iter("BenchmarkResults"):
        name = br.get("name", "unknown")
        mean_el = br.find("mean")
        if mean_el is None:
            continue
        mean_ns = float(mean_el.get("value", 0))
        low_ns = float(mean_el.get("lowerBound", mean_ns))
        high_ns = float(mean_el.get("upperBound", mean_ns))
        results[name] = {
            "mean_ns": mean_ns,
            "low_ns": low_ns,
            "high_ns": high_ns,
            "relative_ci": relative_ci(mean_ns, low_ns, high_ns),
        }
    return results


def relative_ci(mean_ns: float, low_ns: float, high_ns: float) -> float:
    return ((high_ns - low_ns) / (2 * mean_ns)) if mean_ns else 0.0


def benchmark_values(raw: dict) -> dict:
    mean_ns = raw.get("mean_ns", raw.get("mean_ms", 0) * 1e6)
    low_ns = raw.get("low_ns", mean_ns)
    high_ns = raw.get("high_ns", mean_ns)
    return {
        "mean_ns": mean_ns,
        "low_ns": low_ns,
        "high_ns": high_ns,
        "relative_ci": raw.get("relative_ci", relative_ci(mean_ns, low_ns, high_ns)),
    }


def inconclusive_regression(item: dict, threshold: float) -> bool:
    return item.get("status") in {"NOISY CI", "UNSTABLE CI"} and item.get("delta", 0.0) > threshold


def classify(base_raw: dict, current_raw: dict, threshold: float, max_relative_ci: float) -> dict:
    base = benchmark_values(base_raw)
    current = benchmark_values(current_raw)
    base_ms = base["mean_ns"] / 1e6
    current_ms = current["mean_ns"] / 1e6
    delta = (current["mean_ns"] - base["mean_ns"]) / base["mean_ns"] if base["mean_ns"] else 0.0

    base_unstable = max_relative_ci >= 0 and base["relative_ci"] > max_relative_ci
    current_unstable = max_relative_ci >= 0 and current["relative_ci"] > max_relative_ci
    clear_regression = delta > threshold and current["low_ns"] > base["high_ns"]
    clear_improvement = delta < -threshold and current["high_ns"] < base["low_ns"]

    if base_unstable or current_unstable:
        status = "UNSTABLE CI"
    elif clear_regression:
        status = "REGRESSED"
    elif clear_improvement:
        status = "IMPROVED"
    elif abs(delta) > threshold:
        status = "NOISY CI"
    else:
        status = "OK"

    return {
        "base_ms": base_ms,
        "base_low_ms": base["low_ns"] / 1e6,
        "base_high_ms": base["high_ns"] / 1e6,
        "base_relative_ci": base["relative_ci"],
        "current_ms": current_ms,
        "current_low_ms": current["low_ns"] / 1e6,
        "current_high_ms": current["high_ns"] / 1e6,
        "current_relative_ci": current["relative_ci"],
        "delta": delta,
        "status": status,
    }


def run_current_benchmarks(args, xml_path: str) -> int:
    cmd = [args.binary, args.tag, "--reporter", "XML", f"--out={xml_path}",
           "--benchmark-samples", str(args.benchmark_samples)]
    if args.benchmark_warmup_time is not None:
        cmd.extend(["--benchmark-warmup-time", str(args.benchmark_warmup_time)])
    if args.benchmark_resamples is not None:
        cmd.extend(["--benchmark-resamples", str(args.benchmark_resamples)])
    if args.benchmark_confidence_interval is not None:
        cmd.extend(["--benchmark-confidence-interval", str(args.benchmark_confidence_interval)])
    result = subprocess.run(cmd, stderr=subprocess.DEVNULL, timeout=args.timeout)
    return result.returncode


def fmt_interval(mean_ms: float, low_ms: float, high_ms: float, rel_ci: float) -> str:
    return f"{mean_ms:.3f} [{low_ms:.3f},{high_ms:.3f}] +/-{rel_ci * 100:.1f}%"


def main():
    parser = argparse.ArgumentParser(description="Compare CellEvoX benchmarks against baseline")
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--baseline", default=DEFAULT_BASELINE)
    parser.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD,
                        help="Max allowed speed regression as a fraction (default: 0.30 = 30%%)")
    parser.add_argument("--memory-threshold", type=float, default=0.10,
                        help="Max allowed memory regression as a fraction (default: 0.10 = 10%%)")
    parser.add_argument("--max-relative-ci", type=float, default=0.05,
                        help="Max accepted CI half-width divided by mean; negative disables (default: 0.05)")
    parser.add_argument("--fail-on-inconclusive", action="store_true",
                        help="Fail when benchmarks remain noisy or have too-wide confidence intervals")
    parser.add_argument("--fail-on-missing", action="store_true",
                        help="Fail when a baseline benchmark is missing from the current run")
    parser.add_argument("--skip-memory-check", action="store_true",
                        help="Do not fail on Max RSS changes")
    parser.add_argument("--tag", default="[benchmark]")
    parser.add_argument("--benchmark-samples", type=int, default=20,
                        help="Number of Catch2 benchmark samples to collect (default: 20)")
    parser.add_argument("--benchmark-warmup-time", type=int, default=None,
                        help="Catch2 benchmark warmup time in milliseconds")
    parser.add_argument("--benchmark-resamples", type=int, default=None,
                        help="Number of Catch2 bootstrap resamples")
    parser.add_argument("--benchmark-confidence-interval", type=float, default=None,
                        help="Catch2 bootstrap confidence interval, e.g. 0.95 or 0.99")
    parser.add_argument("--timeout", type=int, default=600,
                        help="Benchmark subprocess timeout in seconds (default: 600)")
    parser.add_argument("--report-json", default=None,
                        help="Optional path for a machine-readable comparison report")
    args = parser.parse_args()

    binary = os.path.abspath(args.binary)
    baseline_path = os.path.abspath(args.baseline)

    if not os.path.exists(binary):
        print(f"ERROR: Binary not found: {binary}", file=sys.stderr)
        sys.exit(2)
    if not os.path.exists(baseline_path):
        print(f"ERROR: Baseline not found: {baseline_path}", file=sys.stderr)
        sys.exit(2)

    with open(baseline_path) as f:
        baseline_data = json.load(f)
    baseline = baseline_data.get("benchmarks", {})
    baseline_max_rss = baseline_data.get("max_rss_kb")

    with tempfile.NamedTemporaryFile(suffix=".xml", delete=False) as tmp:
        xml_path = tmp.name

    try:
        print(f"Running benchmarks with tag '{args.tag}' (--benchmark-samples {args.benchmark_samples})...")
        returncode = run_current_benchmarks(args, xml_path)
        current_max_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        if returncode != 0:
            print(f"Benchmark run failed (exit {returncode})", file=sys.stderr)
            sys.exit(2)
        current = parse_xml_benchmarks(xml_path)
    finally:
        if os.path.exists(xml_path):
            os.unlink(xml_path)

    if not current:
        print("ERROR: No benchmark results found in current run.", file=sys.stderr)
        sys.exit(2)

    print(f"\n{'Benchmark':<55} {'Baseline mean [CI]':>34} {'Current mean [CI]':>34} {'Change':>10}  Status")
    print("-" * 145)

    report = {
        "config": {
            "tag": args.tag,
            "threshold": args.threshold,
            "memory_threshold": args.memory_threshold,
            "max_relative_ci": args.max_relative_ci,
            "benchmark_samples": args.benchmark_samples,
            "benchmark_resamples": args.benchmark_resamples,
            "benchmark_confidence_interval": args.benchmark_confidence_interval,
            "fail_on_inconclusive": args.fail_on_inconclusive,
            "fail_on_missing": args.fail_on_missing,
            "skip_memory_check": args.skip_memory_check,
        },
        "benchmarks": {},
        "memory": {},
        "summary": {},
    }

    counts = {"improved": 0, "ok": 0, "noisy": 0, "unstable": 0, "regressed": 0, "new": 0, "missing": 0}
    regressions = []
    inconclusive = []

    for name, cur_raw in sorted(current.items(), key=lambda x: x[0]):
        if name not in baseline:
            counts["new"] += 1
            print(f"  {name:<53} {'N/A':>34} {'NEW':>34} {'NEW':>10}  NEW")
            report["benchmarks"][name] = {"status": "NEW"}
            continue

        item = classify(baseline[name], cur_raw, args.threshold, args.max_relative_ci)
        report["benchmarks"][name] = item
        status = item["status"]
        if status == "REGRESSED":
            counts["regressed"] += 1
            regressions.append((name, item))
        elif status == "IMPROVED":
            counts["improved"] += 1
        elif status == "NOISY CI":
            counts["noisy"] += 1
            inconclusive.append((name, item))
        elif status == "UNSTABLE CI":
            counts["unstable"] += 1
            inconclusive.append((name, item))
        else:
            counts["ok"] += 1

        base_text = fmt_interval(item["base_ms"], item["base_low_ms"], item["base_high_ms"], item["base_relative_ci"])
        cur_text = fmt_interval(item["current_ms"], item["current_low_ms"], item["current_high_ms"], item["current_relative_ci"])
        print(f"  {name:<53} {base_text:>34} {cur_text:>34} {item['delta']:+10.1%}  {status}")

    missing = set(baseline) - set(current)
    for name in sorted(missing):
        counts["missing"] += 1
        print(f"  {name:<53} {'--':>34} {'--':>34} {'MISSING':>10}  MISSING")
        report["benchmarks"][name] = {"status": "MISSING"}

    print("-" * 145)

    memory_regressed = False
    if baseline_max_rss and not args.skip_memory_check:
        mem_delta = (current_max_rss - baseline_max_rss) / baseline_max_rss
        memory_regressed = mem_delta > args.memory_threshold
        mem_status = "REGRESSED" if memory_regressed else "OK"
        report["memory"] = {
            "baseline_max_rss_kb": baseline_max_rss,
            "current_max_rss_kb": current_max_rss,
            "delta": mem_delta,
            "status": mem_status,
        }
        print("\nMemory Usage (Max RSS):")
        print(f"  Baseline: {baseline_max_rss / 1024:.1f} MB | Current: {current_max_rss / 1024:.1f} MB | Change: {mem_delta:+.1%} | {mem_status}")

    report["summary"] = counts
    print("-" * 145)
    print("\nSummary: "
          f"{len(current)} benchmarks run | {counts['improved']} improved | {counts['ok']} ok | "
          f"{counts['noisy']} noisy CI | {counts['unstable']} unstable CI | "
          f"{counts['regressed']} clear regressions | {counts['new']} new | {counts['missing']} missing")

    if regressions:
        print(f"\nSPEED REGRESSION FAILURES (>{args.threshold:.0%} slower and CI-separated):")
        for name, item in regressions:
            print(f"  {name}: {item['base_ms']:.3f}ms [{item['base_low_ms']:.3f},{item['base_high_ms']:.3f}] -> "
                  f"{item['current_ms']:.3f}ms [{item['current_low_ms']:.3f},{item['current_high_ms']:.3f}] ({item['delta']:+.1%})")

    inconclusive_failures = [
        (name, item) for name, item in inconclusive if inconclusive_regression(item, args.threshold)
    ]
    if inconclusive_failures and args.fail_on_inconclusive:
        print("\nINCONCLUSIVE REGRESSION RISKS (failing because --fail-on-inconclusive is set):")
        for name, item in inconclusive_failures:
            print(f"  {name}: {item['status']} (baseline +/-{item['base_relative_ci'] * 100:.1f}%, current +/-{item['current_relative_ci'] * 100:.1f}%, change {item['delta']:+.1%})")

    if args.report_json:
        os.makedirs(os.path.dirname(os.path.abspath(args.report_json)), exist_ok=True)
        with open(args.report_json, "w") as f:
            json.dump(report, f, indent=2)

    missing_failed = args.fail_on_missing and counts["missing"]
    if regressions or memory_regressed or missing_failed or (args.fail_on_inconclusive and inconclusive_failures):
        print("\nBenchmark gate failed.")
        sys.exit(1)

    print("\nAll benchmarks within configured thresholds.")
    sys.exit(0)


if __name__ == "__main__":
    main()
