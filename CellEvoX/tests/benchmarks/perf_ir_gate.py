#!/usr/bin/env python3
"""
perf_ir_gate.py - Deterministic retired-instruction (Ir) gate for CI.

Wall-clock timings on shared CI runners are noisy. Retired instruction counts
collected by Valgrind Callgrind are not: the probe binary (tests/perf_probe.cpp)
runs a fixed number of simulation steps with a fixed random seed and a single
TBB thread, so both the work and the instruction count are reproducible.

The gate builds nothing. It is given a base and a head probe binary, measures
each workload under Callgrind, and compares Ir:

  * delta > --threshold, or its
    --accepted-delta if named     -> REGRESSED (blocks)
  * delta < -threshold            -> IMPROVED
  * otherwise                     -> OK
  * checksum differs              -> SEMANTICS CHANGED (blocks: the two binaries
                                     no longer do the same work, so Ir is not
                                     comparable)
  * repeated runs of one side
    differ by more than --max-spread -> NON-DETERMINISTIC (blocks)
  * missing binary / crash /
    timeout / unparsable output   -> MISSING (blocks)

Everything that is not a measured, deterministic, semantically identical
comparison fails the gate. There is no skip path.

A workload named in ``--accepted-delta WORKLOAD=RATIO`` may grow by that ratio
instead of ``--threshold``, and its row is printed as ``OK (accepted +N%)``. The
ratio must exceed ``--threshold``: an exception can only relax a workload, never
tighten it. This is for a cost the project has decided to pay in the current PR,
not for widening the gate: it is scoped to one workload and visible in the CI
invocation, so the reviewer sees it.

Instruction collection is limited to the probe's ``main`` with
``--collect-atstart=no --toggle-collect=main``. Anything the dynamic loader
does before ``main`` is not reproducible (symbol lookup and relocation costs
vary between processes) and would otherwise dominate the noise floor.

Each side is measured ``--runs`` times (default 2) and the minimum is used.
Measurement noise can only add instructions, so the minimum is the best
estimate of the deterministic workload cost.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional

WORKLOADS = [
    "stochastic-step-baseline",
    "stochastic-step-mutations",
    "stochastic-step-pressure",
    "run-5-steps",
    "common-step-baseline",
    "realistic-2d-450",
]

CHECKSUM_RE = re.compile(r"RESULT .*checksum=0x([0-9a-fA-F]+)")
SUMMARY_RE = re.compile(r"^summary:\s*(\d+)\s*$", re.MULTILINE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Deterministic instruction-count (Ir) gate")
    parser.add_argument("--base-binary", required=True, help="Probe binary built from the base revision")
    parser.add_argument("--head-binary", required=True, help="Probe binary built from the head revision")
    parser.add_argument("--workloads", default=",".join(WORKLOADS),
                        help="Comma-separated workload names (default: all)")
    parser.add_argument("--threshold", type=float, default=0.02,
                        help="Maximum allowed Ir increase before the gate blocks (default: 2%%)")
    parser.add_argument("--accepted-delta", action="append", default=[], metavar="WORKLOAD=RATIO",
                        help="Allow one workload to grow by RATIO (e.g. realistic-2d-450=0.03) as a cost "
                             "the project has accepted; repeatable, and the row is reported as an "
                             "accepted cost instead of a silent pass")
    parser.add_argument("--max-spread", type=float, default=0.02,
                        help="Maximum spread between repeated runs of one side; a wider spread means "
                             "the workload is not deterministic (default: 2%%)")
    parser.add_argument("--runs", type=int, default=2,
                        help="Callgrind runs per side per workload; the minimum Ir is used (default: 2)")
    parser.add_argument("--valgrind", default="valgrind", help="Valgrind executable (default: valgrind)")
    parser.add_argument("--timeout", type=int, default=300, help="Timeout per Callgrind run in seconds")
    parser.add_argument("--work-dir", default="/tmp/cellevox_ir_gate")
    parser.add_argument("--report-json", default=None)
    args = parser.parse_args()
    if args.runs < 2:
        parser.error("--runs must be at least 2: the repeat-spread determinism check needs two runs")
    args.accepted_delta = parse_accepted_deltas(args.accepted_delta, parser, args.threshold)
    return args


def parse_accepted_deltas(entries: List[str], parser: argparse.ArgumentParser,
                          threshold: float) -> Dict[str, float]:
    accepted: Dict[str, float] = {}
    for entry in entries:
        name, _, ratio = entry.partition("=")
        name = name.strip()
        try:
            value = float(ratio)
        except ValueError:
            parser.error(f"--accepted-delta expects WORKLOAD=RATIO, got {entry!r}")
        if name not in WORKLOADS:
            parser.error(f"--accepted-delta: unknown workload {name!r}")
        if not 0 < value <= 0.5:
            parser.error(f"--accepted-delta: {name} ratio must be in (0, 0.5], got {value}")
        if value <= threshold:
            parser.error(f"--accepted-delta: {name} ratio {value} must exceed --threshold "
                         f"{threshold}; an exception can only relax a workload")
        if name in accepted:
            parser.error(f"--accepted-delta: {name} given more than once")
        accepted[name] = value
    return accepted


def measure(side: str, binary: Path, workload: str, run_index: int, args: argparse.Namespace) -> Dict:
    """Run one Callgrind measurement and return {ir, checksum} or {error}."""
    # Include the PID so two concurrent gate invocations on one host do not
    # delete and recreate each other's measurement directory.
    run_dir = Path(args.work_dir) / f"{workload}-{side}-{run_index}-{os.getpid()}"
    out_file = run_dir / "callgrind.out"
    # Start every measurement from a nonexistent stats directory: the probe's
    # cleanup cost depends on how much the previous run left behind, and that
    # cost is inside the measured window.
    shutil.rmtree(run_dir, ignore_errors=True)
    run_dir.mkdir(parents=True)
    cmd = [
        args.valgrind,
        "--tool=callgrind",
        "--collect-atstart=no",
        "--toggle-collect=main",
        f"--callgrind-out-file={out_file}",
        str(binary),
        "--workload", workload,
        "--iterations", "1",
        "--stats-dir", str(run_dir / "stats"),
    ]
    try:
        completed = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return {"error": f"timeout after {args.timeout}s"}
    except OSError as exc:
        return {"error": f"cannot execute: {exc}"}

    if completed.returncode != 0:
        tail = (completed.stderr or completed.stdout or "").strip().splitlines()[-3:]
        return {"error": f"exit {completed.returncode}: {' / '.join(tail)}"}

    try:
        text = out_file.read_text()
    except OSError as exc:
        return {"error": f"callgrind output not readable: {exc}"}

    summaries = SUMMARY_RE.findall(text)
    if len(summaries) != 1:
        # Collection shape is part of what Ir means: a second summary means a
        # helper thread or an extra toggle, so summing would compare different
        # work rather than report a change.
        return {"error": f"expected exactly 1 callgrind summary, got {len(summaries)}"}
    ir = int(summaries[0])
    if ir <= 0:
        return {"error": "no instructions collected (probe 'main' not toggled?)"}

    match = CHECKSUM_RE.search(completed.stdout)
    if not match:
        return {"error": "probe did not print a RESULT checksum line"}

    return {"ir": ir, "checksum": match.group(1)}


def measure_side(side: str, binary: Path, workload: str, args: argparse.Namespace) -> Dict:
    runs = [measure(side, binary, workload, index, args) for index in range(1, args.runs + 1)]
    errors = [run["error"] for run in runs if "error" in run]
    if errors:
        return {"error": "; ".join(errors)}
    counts = [run["ir"] for run in runs]
    checksums = {run["checksum"] for run in runs}
    return {
        "ir": min(counts),
        "runs": counts,
        "spread": (max(counts) - min(counts)) / min(counts),
        "checksums": sorted(checksums),
    }


def evaluate(workload: str, base: Dict, head: Dict, args: argparse.Namespace) -> Dict:
    entry = {"workload": workload, "base": base, "head": head}
    if "error" in base or "error" in head:
        entry["status"] = "MISSING"
        entry["detail"] = base.get("error") or head.get("error")
        return entry
    if len(base["checksums"]) > 1:
        entry["status"] = "NON-DETERMINISTIC"
        entry["detail"] = f"base runs produced different checksums: {base['checksums']}"
        return entry
    if len(head["checksums"]) > 1:
        entry["status"] = "NON-DETERMINISTIC"
        entry["detail"] = f"head runs produced different checksums: {head['checksums']}"
        return entry
    worst_spread = max(base["spread"], head["spread"])
    if worst_spread > args.max_spread:
        entry["status"] = "NON-DETERMINISTIC"
        entry["detail"] = f"spread {worst_spread:.2%} exceeds {args.max_spread:.2%}"
        return entry
    if base["checksums"] != head["checksums"]:
        entry["status"] = "SEMANTICS CHANGED"
        entry["detail"] = f"base=0x{base['checksums'][0]} head=0x{head['checksums'][0]}"
        return entry
    delta = (head["ir"] - base["ir"]) / base["ir"]
    # Defensive: parse_accepted_deltas already rejects a ratio at or below the
    # threshold, but the max keeps a future parser change from tightening a
    # workload below the global threshold it is an exception to.
    allowed = max(args.threshold, args.accepted_delta.get(workload, args.threshold))
    entry["delta"] = delta
    entry["allowed"] = allowed
    if delta > allowed:
        entry["status"] = "REGRESSED"
    elif delta < -args.threshold:
        entry["status"] = "IMPROVED"
    else:
        entry["status"] = "OK"
        if delta > args.threshold:
            entry["accepted"] = allowed
    return entry


def print_report(results: List[Dict], args: argparse.Namespace) -> None:
    print(f"\n=== Deterministic instruction gate (threshold {args.threshold:+.1%}, "
          f"{args.runs} runs per side, minimum Ir) ===")
    accepted = ", ".join(f"{name} {ratio:+.1%}" for name, ratio in sorted(args.accepted_delta.items()))
    if accepted:
        print(f"    accepted deltas: {accepted}")
    print(f"{'Workload':<28} {'Base Ir':>15} {'Head Ir':>15} {'Change':>9}  Status")
    print("-" * 82)
    for entry in results:
        status = entry["status"]
        if status in {"MISSING", "NON-DETERMINISTIC", "SEMANTICS CHANGED"}:
            print(f"  {entry['workload']:<26} {'--':>15} {'--':>15} {'--':>9}  {status}")
            if entry.get("detail"):
                print(f"  {'':<26} {entry['detail']}")
            continue
        status_text = status
        if entry.get("accepted") is not None:
            status_text = f"OK (accepted +{entry['accepted']:.1%})"
        print(f"  {entry['workload']:<26} {entry['base']['ir']:>15,} {entry['head']['ir']:>15,} "
              f"{entry['delta']:+9.2%}  {status_text}")
    print("-" * 82)
    failures = [entry for entry in results if entry["status"] in
                {"REGRESSED", "MISSING", "NON-DETERMINISTIC", "SEMANTICS CHANGED"}]
    print(f"Summary: {len(results) - len(failures)} passed | {len(failures)} failed")


def main() -> int:
    args = parse_args()
    workloads = [item.strip() for item in args.workloads.split(",") if item.strip()]
    if not workloads:
        raise SystemExit("No workloads selected: --workloads must name at least one workload.")
    unknown = [item for item in workloads if item not in WORKLOADS]
    if unknown:
        raise SystemExit(f"Unknown workload(s): {', '.join(unknown)}. Known: {', '.join(WORKLOADS)}")

    base_binary = Path(args.base_binary)
    head_binary = Path(args.head_binary)
    for label, binary in (("base", base_binary), ("head", head_binary)):
        if not binary.is_file():
            raise SystemExit(f"{label} probe binary not found: {binary}")

    results = []
    for workload in workloads:
        print(f"\nMeasuring {workload} ...", flush=True)
        base = measure_side("base", base_binary, workload, args)
        head = measure_side("head", head_binary, workload, args)
        entry = evaluate(workload, base, head, args)
        results.append(entry)
        print(f"  -> {entry['status']}", flush=True)

    print_report(results, args)

    if args.report_json:
        report_path = Path(args.report_json)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        with report_path.open("w") as handle:
            json.dump({
                "config": {
                    "threshold": args.threshold,
                    "max_spread": args.max_spread,
                    "runs": args.runs,
                    "workloads": workloads,
                    "accepted_delta": args.accepted_delta,
                },
                "results": results,
            }, handle, indent=2)

    failed = any(entry["status"] in {"REGRESSED", "MISSING", "NON-DETERMINISTIC", "SEMANTICS CHANGED"}
                 for entry in results)
    if failed:
        print("\nDeterministic instruction gate failed.")
        return 1
    print("\nDeterministic instruction gate passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
