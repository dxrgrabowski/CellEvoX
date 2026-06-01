#!/usr/bin/env python3
"""Generate a lightweight source coverage report from gcov JSON output.

This intentionally depends only on GCC's gcov and Python's standard library.
It is meant for regression-risk discovery, not for enforcing a magic percent.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable


@dataclass
class FileCoverage:
    line_counts: dict[int, int] = field(default_factory=dict)

    def add_line(self, line_number: int, count: int) -> None:
        self.line_counts[line_number] = self.line_counts.get(line_number, 0) + count

    @property
    def total(self) -> int:
        return len(self.line_counts)

    @property
    def covered(self) -> int:
        return sum(1 for count in self.line_counts.values() if count > 0)

    @property
    def uncovered_lines(self) -> set[int]:
        return {line_number for line_number, count in self.line_counts.items() if count == 0}

    @property
    def percent(self) -> float:
        return (self.covered / self.total * 100.0) if self.total else 100.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate CellEvoX gcov line coverage report")
    parser.add_argument("--build-dir", required=True, help="Coverage build directory")
    parser.add_argument("--root", default=".", help="Repository root, default: current directory")
    parser.add_argument("--gcov", default="gcov", help="gcov executable")
    parser.add_argument("--include", action="append", default=None,
                        help="Path prefix to include, relative to root. Can be repeated.")
    parser.add_argument("--exclude", action="append", default=None,
                        help="Path prefix to exclude, relative to root. Can be repeated.")
    parser.add_argument("--top", type=int, default=25, help="Number of least-covered files to print")
    parser.add_argument("--json-out", default=None, help="Optional machine-readable report path")
    return parser.parse_args()


def as_posix(path: Path) -> str:
    return path.as_posix()


def normalize_source_path(raw_file: str, root: Path) -> str | None:
    path = Path(raw_file)
    candidates: list[Path] = []
    if path.is_absolute():
        candidates.append(path)
    else:
        candidates.append(root / path)
        candidates.append(Path.cwd() / path)

    for candidate in candidates:
        try:
            resolved = candidate.resolve(strict=False)
            return as_posix(resolved.relative_to(root))
        except ValueError:
            continue
    return None


def included(path: str, includes: Iterable[str], excludes: Iterable[str]) -> bool:
    return any(path == prefix or path.startswith(prefix.rstrip("/") + "/") for prefix in includes) and not any(
        path == prefix or path.startswith(prefix.rstrip("/") + "/") for prefix in excludes
    )


def run_gcov(gcov: str, build_dir: Path, out_dir: Path) -> None:
    gcda_files = sorted(build_dir.rglob("*.gcda"))
    if not gcda_files:
        raise SystemExit(f"No .gcda files found under {build_dir}; run tests from the coverage build first")

    for gcda in gcda_files:
        cmd = [gcov, "--json-format", "--preserve-paths", "--object-directory", str(gcda.parent), str(gcda)]
        result = subprocess.run(cmd, cwd=out_dir, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            raise SystemExit(f"gcov failed for {gcda}:\n{result.stderr}")


def merge_json_reports(report_dir: Path, root: Path, includes: list[str], excludes: list[str]) -> dict[str, FileCoverage]:
    coverage: dict[str, FileCoverage] = {}
    for report in sorted(report_dir.glob("*.gcov.json.gz")):
        with gzip.open(report, "rt", encoding="utf-8") as handle:
            data = json.load(handle)
        for file_item in data.get("files", []):
            rel_path = normalize_source_path(file_item.get("file", ""), root)
            if rel_path is None or not included(rel_path, includes, excludes):
                continue

            item = coverage.setdefault(rel_path, FileCoverage())
            seen_lines = set()
            for line in file_item.get("lines", []):
                line_number = int(line.get("line_number", 0))
                if line_number <= 0 or line_number in seen_lines:
                    continue
                seen_lines.add(line_number)
                count = int(line.get("count", 0))
                item.add_line(line_number, count)
    return coverage


def compact_ranges(lines: Iterable[int], limit: int = 24) -> str:
    ordered = sorted(lines)
    ranges: list[str] = []
    index = 0
    while index < len(ordered) and len(ranges) < limit:
        start = ordered[index]
        end = start
        while index + 1 < len(ordered) and ordered[index + 1] == end + 1:
            index += 1
            end = ordered[index]
        ranges.append(str(start) if start == end else f"{start}-{end}")
        index += 1
    if index < len(ordered):
        ranges.append("...")
    return ",".join(ranges)


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    build_dir = Path(args.build_dir).resolve()
    includes = args.include or ["CellEvoX/src", "CellEvoX/include"]
    excludes = args.exclude or ["CellEvoX/include/external", "CellEvoX/src/main.cpp"]

    if shutil.which(args.gcov) is None:
        raise SystemExit(f"gcov executable not found: {args.gcov}")

    with tempfile.TemporaryDirectory(prefix="cellevox-gcov-") as tmp:
        report_dir = Path(tmp)
        run_gcov(args.gcov, build_dir, report_dir)
        coverage = merge_json_reports(report_dir, root, includes, excludes)

    total_lines = sum(item.total for item in coverage.values())
    covered_lines = sum(item.covered for item in coverage.values())
    overall = (covered_lines / total_lines * 100.0) if total_lines else 100.0

    print(f"Coverage root: {root}")
    print(f"Included: {', '.join(includes)}")
    print(f"Excluded: {', '.join(excludes)}")
    print(f"Overall line coverage: {covered_lines}/{total_lines} ({overall:.1f}%)")
    print("\nLeast-covered files:")
    print(f"{'Coverage':>10} {'Covered/Total':>15}  File")
    print("-" * 88)

    rows = sorted(coverage.items(), key=lambda kv: (kv[1].percent, -kv[1].total, kv[0]))
    for path, item in rows[: args.top]:
        print(f"{item.percent:9.1f}% {item.covered:6d}/{item.total:<8d}  {path}")
        if item.uncovered_lines:
            print(f"{'':>27}  uncovered: {compact_ranges(item.uncovered_lines)}")

    if args.json_out:
        output = {
            "overall": {"covered": covered_lines, "total": total_lines, "percent": overall},
            "files": {
                path: {
                    "covered": item.covered,
                    "total": item.total,
                    "percent": item.percent,
                    "uncovered_lines": sorted(item.uncovered_lines),
                }
                for path, item in sorted(coverage.items())
            },
        }
        json_path = Path(args.json_out)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(output, indent=2))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
