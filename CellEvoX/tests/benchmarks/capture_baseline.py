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
        low_el = br.find("lowerBound")
        high_el = br.find("upperBound")
        if mean_el is None:
            continue

        mean_ns = float(mean_el.get("value", 0))
        low_ns = float(low_el.get("value", 0)) if low_el is not None else mean_ns
        high_ns = float(high_el.get("value", 0)) if high_el is not None else mean_ns

        results[name] = {
            "mean_ns": mean_ns,
            "low_ns": low_ns,
            "high_ns": high_ns,
            "mean_ms": round(mean_ns / 1e6, 4),
        }
    return results


def main():
    parser = argparse.ArgumentParser(description="Capture CellEvoX benchmark baseline")
    parser.add_argument("--binary", default=DEFAULT_BINARY, help="Path to CellEvoXTests binary")
    parser.add_argument("--out", default=DEFAULT_OUT, help="Output baseline JSON file path")
    parser.add_argument("--tag", default="[benchmark]", help="Catch2 tag to run (default: [benchmark])")
    args = parser.parse_args()

    binary = os.path.abspath(args.binary)
    if not os.path.exists(binary):
        print(f"ERROR: Binary not found: {binary}", file=sys.stderr)
        print("Build with: cmake --build build --target CellEvoXTests", file=sys.stderr)
        sys.exit(1)

    with tempfile.NamedTemporaryFile(suffix=".xml", delete=False) as tmp:
        xml_path = tmp.name

    try:
        print(f"Running benchmarks with tag '{args.tag}' (--benchmark-samples 20)...")
        result = subprocess.run(
            [binary, args.tag, "--reporter", "XML", f"--out={xml_path}", "--benchmark-samples", "20"],
            stderr=subprocess.DEVNULL,  # suppress spdlog noise
            timeout=600,
        )
        max_rss_kb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        
        if result.returncode != 0 and result.returncode != 1:
            print(f"Benchmark run failed with exit code {result.returncode}", file=sys.stderr)
            sys.exit(1)

        benchmarks = parse_xml_benchmarks(xml_path)
        if not benchmarks:
            print("ERROR: No benchmark results found in output. Check [benchmark] tag.", file=sys.stderr)
            sys.exit(1)

        out_path = os.path.abspath(args.out)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w") as f:
            json.dump({"max_rss_kb": max_rss_kb, "benchmarks": benchmarks}, f, indent=2)

        print(f"\n✅ Captured {len(benchmarks)} benchmarks (Max RSS: {max_rss_kb / 1024:.1f} MB) → {out_path}")
        print("\nBaseline summary (mean ms):")
        for name, v in sorted(benchmarks.items(), key=lambda x: x[1]["mean_ms"]):
            print(f"  {v['mean_ms']:>10.4f} ms  {name}")
    finally:
        os.unlink(xml_path)


if __name__ == "__main__":
    main()
