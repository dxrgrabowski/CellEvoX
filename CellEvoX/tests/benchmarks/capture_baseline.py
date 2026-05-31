#!/usr/bin/env python3
"""
capture_baseline.py — Record benchmark results as a JSON baseline file.

Usage:
    python3 tests/benchmarks/capture_baseline.py [--out tests/benchmarks/baseline.json]

This script:
  1. Runs CellEvoXTests "[benchmark]" with the XML reporter.
  2. Parses each <BenchmarkResults> element to extract the mean nanoseconds.
  3. Writes a baseline.json file you should commit to the repo.
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
DEFAULT_OUT = os.path.join(os.path.dirname(__file__), "baseline.json")


def parse_xml_benchmarks(xml_path: str) -> dict:
    tree = ET.parse(xml_path)
    root = tree.getroot()

    results = {}
    # BenchmarkResults are nested inside TestCase > Section > BenchmarkResults
    for br in root.iter("BenchmarkResults"):
        name = br.get("name", "unknown")
        mean_el = br.find("mean")
        if mean_el is None:
            continue

        mean_ns = float(mean_el.get("value", 0))
        low_ns = float(mean_el.get("lowerBound", mean_ns))
        high_ns = float(mean_el.get("upperBound", mean_ns))

        relative_ci = ((high_ns - low_ns) / (2 * mean_ns)) if mean_ns else 0.0
        results[name] = {
            "mean_ns": mean_ns,
            "low_ns": low_ns,
            "high_ns": high_ns,
            "relative_ci": relative_ci,
            "mean_ms": round(mean_ns / 1e6, 4),
        }
    return results


def main():
    parser = argparse.ArgumentParser(description="Capture CellEvoX benchmark baseline")
    parser.add_argument("--binary", default=DEFAULT_BINARY, help="Path to CellEvoXTests binary")
    parser.add_argument("--out", default=DEFAULT_OUT, help="Output baseline JSON file path")
    parser.add_argument("--tag", default="[benchmark]", help="Catch2 tag to run (default: [benchmark])")
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
    args = parser.parse_args()

    binary = os.path.abspath(args.binary)
    if not os.path.exists(binary):
        print(f"ERROR: Binary not found: {binary}", file=sys.stderr)
        print("Build with: cmake --build build --target CellEvoXTests", file=sys.stderr)
        sys.exit(1)

    with tempfile.NamedTemporaryFile(suffix=".xml", delete=False) as tmp:
        xml_path = tmp.name

    try:
        print(f"Running benchmarks with tag '{args.tag}' (--benchmark-samples {args.benchmark_samples})...")
        cmd = [binary, args.tag, "--reporter", "XML", f"--out={xml_path}",
               "--benchmark-samples", str(args.benchmark_samples)]
        if args.benchmark_warmup_time is not None:
            cmd.extend(["--benchmark-warmup-time", str(args.benchmark_warmup_time)])
        if args.benchmark_resamples is not None:
            cmd.extend(["--benchmark-resamples", str(args.benchmark_resamples)])
        if args.benchmark_confidence_interval is not None:
            cmd.extend(["--benchmark-confidence-interval", str(args.benchmark_confidence_interval)])
        result = subprocess.run(
            cmd,
            stderr=subprocess.DEVNULL,  # suppress spdlog noise
            timeout=args.timeout,
        )
        max_rss_kb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        
        if result.returncode != 0:
            print(f"Benchmark run failed with exit code {result.returncode}", file=sys.stderr)
            sys.exit(1)

        benchmarks = parse_xml_benchmarks(xml_path)
        if not benchmarks:
            print("ERROR: No benchmark results found in output. Check [benchmark] tag.", file=sys.stderr)
            sys.exit(1)

        out_path = os.path.abspath(args.out)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w") as f:
            json.dump({
                "metadata": {
                    "tag": args.tag,
                    "benchmark_samples": args.benchmark_samples,
                    "benchmark_warmup_time_ms": args.benchmark_warmup_time,
                    "benchmark_resamples": args.benchmark_resamples,
                    "benchmark_confidence_interval": args.benchmark_confidence_interval,
                },
                "max_rss_kb": max_rss_kb,
                "benchmarks": benchmarks,
            }, f, indent=2)

        print(f"\n✅ Captured {len(benchmarks)} benchmarks (Max RSS: {max_rss_kb / 1024:.1f} MB) → {out_path}")
        print("\nBaseline summary (mean ms):")
        for name, v in sorted(benchmarks.items(), key=lambda x: x[1]["mean_ms"]):
            print(f"  {v['mean_ms']:>10.4f} ms  +/- {v['relative_ci'] * 100:>5.2f}%  {name}")
    finally:
        os.unlink(xml_path)


if __name__ == "__main__":
    main()
