#!/usr/bin/env python3
"""
compare_benchmarks.py — Compare current benchmark results against a stored baseline.

Usage:
    python3 tests/benchmarks/compare_benchmarks.py [--threshold 0.20] [--baseline baseline.json]

Exit codes:
    0  All benchmarks within threshold (CI passes)
    1  One or more benchmarks regressed beyond threshold (CI fails)
    2  Script error (missing binary, baseline, etc.)

The threshold is a fraction, e.g. 0.20 = 20% regression allowed.
"""

import argparse
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
import tempfile
import resource

DEFAULT_BINARY = os.path.join(os.path.dirname(__file__), "..", "..", "build", "bin", "CellEvoXTests")
DEFAULT_BASELINE = os.path.join(os.path.dirname(__file__), "baseline.json")
# 30% is the practical threshold for stochastic simulations — the run-to-run variance
# for short-running benchmarks is inherently ~10-20%, so we want to catch only
# regressions that are clearly larger than noise (e.g. new O(N^2) loop added).
# For CI stability: tighten to 0.20 only for long-running benchmarks where variance is low.
DEFAULT_THRESHOLD = 0.30   # 30% regression tolerance



def parse_xml_benchmarks(xml_path: str) -> dict:
    tree = ET.parse(xml_path)
    root = tree.getroot()
    results = {}
    for br in root.iter("BenchmarkResults"):
        name = br.get("name", "unknown")
        mean_el = br.find("mean")
        if mean_el is None:
            continue
        results[name] = {"mean_ns": float(mean_el.get("value", 0))}
    return results


def main():
    parser = argparse.ArgumentParser(description="Compare CellEvoX benchmarks against baseline")
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--baseline", default=DEFAULT_BASELINE)
    parser.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD,
                        help="Max allowed speed regression as a fraction (default: 0.30 = 30%%)")
    parser.add_argument("--memory-threshold", type=float, default=0.10,
                        help="Max allowed memory regression as a fraction (default: 0.10 = 10%%)")
    parser.add_argument("--tag", default="[benchmark]")
    args = parser.parse_args()

    # --- Validate inputs ---
    binary = os.path.abspath(args.binary)
    baseline_path = os.path.abspath(args.baseline)

    if not os.path.exists(binary):
        print(f"ERROR: Binary not found: {binary}", file=sys.stderr)
        sys.exit(2)
    if not os.path.exists(baseline_path):
        print(f"ERROR: Baseline not found: {baseline_path}", file=sys.stderr)
        print("Run: python3 tests/benchmarks/capture_baseline.py", file=sys.stderr)
        sys.exit(2)

    with open(baseline_path) as f:
        baseline_data = json.load(f)
    baseline = baseline_data.get("benchmarks", {})
    baseline_max_rss = baseline_data.get("max_rss_kb")

    # --- Run current benchmarks ---
    with tempfile.NamedTemporaryFile(suffix=".xml", delete=False) as tmp:
        xml_path = tmp.name

    try:
        print(f"Running benchmarks with tag '{args.tag}' (--benchmark-samples 20)...")
        result = subprocess.run(
            [binary, args.tag, "--reporter", "XML", f"--out={xml_path}", "--benchmark-samples", "20"],
            stderr=subprocess.DEVNULL,
            timeout=600,
        )
        current_max_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        
        if result.returncode not in (0, 1):
            print(f"Benchmark run failed (exit {result.returncode})", file=sys.stderr)
            sys.exit(2)

        current = parse_xml_benchmarks(xml_path)
    finally:
        if os.path.exists(xml_path):
            os.unlink(xml_path)

    if not current:
        print("ERROR: No benchmark results found in current run.", file=sys.stderr)
        sys.exit(2)

    # --- Compare ---
    RESET  = "\033[0m"
    RED    = "\033[91m"
    GREEN  = "\033[92m"
    YELLOW = "\033[93m"
    BOLD   = "\033[1m"

    print(f"\n{'Benchmark':<55} {'Baseline':>12} {'Current':>12} {'Change':>10}  Status")
    print("─" * 100)

    regressions = []
    improvements = []
    new_benchmarks = []

    for name, cur in sorted(current.items(), key=lambda x: x[0]):
        cur_ms = cur["mean_ns"] / 1e6
        if name not in baseline:
            new_benchmarks.append(name)
            print(f"  {name:<53} {'N/A':>12} {cur_ms:>11.3f}ms {'NEW':>10}  {YELLOW}NEW{RESET}")
            continue

        base_ms = baseline[name]["mean_ms"]
        delta = (cur_ms - base_ms) / base_ms  # positive = slower

        if delta > args.threshold:
            status = f"{RED}REGRESSED  ▲{RESET}"
            regressions.append((name, base_ms, cur_ms, delta))
        elif delta < -args.threshold:
            status = f"{GREEN}IMPROVED   ▼{RESET}"
            improvements.append((name, base_ms, cur_ms, delta))
        else:
            status = f"{GREEN}OK{RESET}"

        change_str = f"{delta:+.1%}"
        print(f"  {name:<53} {base_ms:>11.3f}ms {cur_ms:>11.3f}ms {change_str:>10}  {status}")

    # Benchmarks that are in baseline but not in current run
    missing = set(baseline) - set(current)
    for name in sorted(missing):
        print(f"  {name:<53} {'--':>12} {'--':>12} {'MISSING':>10}  {YELLOW}⚠ NOT RUN{RESET}")

    print("─" * 100)
    
    memory_regressed = False
    if baseline_max_rss:
        mem_delta = (current_max_rss - baseline_max_rss) / baseline_max_rss
        mem_status = f"{GREEN}OK{RESET}"
        if mem_delta > args.memory_threshold:
            mem_status = f"{RED}REGRESSED  ▲{RESET}"
            memory_regressed = True
        elif mem_delta < -args.memory_threshold:
            mem_status = f"{GREEN}IMPROVED   ▼{RESET}"
            
        print(f"\n{BOLD}Memory Usage (Max RSS){RESET}:")
        print(f"  Baseline: {baseline_max_rss / 1024:.1f} MB  |  Current: {current_max_rss / 1024:.1f} MB  |  Change: {mem_delta:+.1%}  |  {mem_status}")

    print("─" * 100)
    print(f"\nSummary: {len(current)} benchmarks run | "
          f"{GREEN}{len(improvements)} improved{RESET} | "
          f"{len(current) - len(regressions) - len(improvements) - len(new_benchmarks)} within threshold | "
          f"{RED}{len(regressions)} regressed{RESET} | "
          f"{YELLOW}{len(new_benchmarks)} new{RESET}")

    if regressions or memory_regressed:
        if regressions:
            print(f"\n{RED}{BOLD}SPEED REGRESSION FAILURES (>{args.threshold:.0%} slower than baseline):{RESET}")
            for name, base, cur, delta in regressions:
                print(f"  {RED}▲ {name}{RESET}: {base:.3f}ms → {cur:.3f}ms ({delta:+.1%})")
        if memory_regressed:
            print(f"\n{RED}{BOLD}MEMORY REGRESSION FAILURE (>{args.memory_threshold:.0%} higher RSS than baseline):{RESET}")
            print(f"  {RED}▲ Max RSS{RESET}: {baseline_max_rss / 1024:.1f} MB → {current_max_rss / 1024:.1f} MB ({mem_delta:+.1%})")
            
        print(f"\n{RED}❌ Benchmark regression detected. Investigate before merging.{RESET}")
        sys.exit(1)
    else:
        print(f"\n{GREEN}✅ All benchmarks within thresholds (Speed: {args.threshold:.0%}, Memory: {args.memory_threshold:.0%}).{RESET}")
        sys.exit(0)


if __name__ == "__main__":
    main()
