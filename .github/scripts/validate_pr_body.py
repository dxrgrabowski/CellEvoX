#!/usr/bin/env python3
"""Check the basic structure of a pull request body.

This is a shape check, not a review. It can tell whether a section is present
and answered, whether template scaffolding was left behind, and whether the
verification section holds anything beyond an empty code block. It cannot tell
whether a criterion is right, whether the user approved a decision, or whether
the change works — those stay with the reviewer.

Deterministic: no model, no network, no diff. Structure is read only outside
fenced code blocks, so a command's quoted output is never mistaken for a
heading or a label.

Usage:
    validate_pr_body.py --body-file pr_body.md [--root .]
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

REQUIRED_SECTIONS = ("What changed and why", "Acceptance criteria", "Verification")
ONE_LINE_LABELS = ("Verified commit:", "Not checked:")
FULL_SHA = re.compile(r"^[0-9a-f]{40}$")
FENCE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")
SECTION_HEADING = re.compile(r"^##\s+(.+?)\s*$")
ANY_HEADING = re.compile(r"^#{1,6}\s+\S")


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)

    def warn(self, message: str) -> None:
        self.warnings.append(message)


def marked_lines(body: str) -> list[tuple[str, bool]]:
    """(line, inside_fence) for each line; fence delimiters count as inside."""
    marked: list[tuple[str, bool]] = []
    fence: str | None = None
    for line in body.splitlines():
        marker = FENCE.match(line)
        if fence is None:
            if marker:
                fence = marker.group(1)
                marked.append((line, True))
            else:
                marked.append((line, False))
        else:
            marked.append((line, True))
            if marker and marker.group(1)[0] == fence[0] and len(marker.group(1)) >= len(fence):
                fence = None
    return marked


def outside_lines(body: str) -> list[str]:
    return [line for line, fenced in marked_lines(body) if not fenced]


def label_values(body: str, label: str) -> list[str]:
    """Every 'Label: value' line outside code blocks, in document order."""
    pattern = re.compile(rf"^{re.escape(label)}[ \t]*(.*)$")
    values: list[str] = []
    for line in outside_lines(body):
        match = pattern.match(line)
        if match:
            values.append(match.group(1).strip())
    return values


def sections(body: str) -> dict[str, list[str]]:
    """Level-2 section contents keyed by lower-cased title, fences included."""
    lines = marked_lines(body)
    starts: list[tuple[int, str]] = []
    for index, (line, fenced) in enumerate(lines):
        if fenced:
            continue
        match = SECTION_HEADING.match(line)
        if match:
            starts.append((index, match.group(1).strip().lower()))

    found: dict[str, list[str]] = {}
    for position, (start, title) in enumerate(starts):
        end = starts[position + 1][0] if position + 1 < len(starts) else len(lines)
        found[title] = [line for line, _ in lines[start + 1:end]]
    return found


def has_answer(lines: list[str]) -> bool:
    """True when a section holds text other than headings, labels, and fences.

    Fenced content counts: a quoted command's output is evidence. An empty code
    block is only two fence markers, so it answers nothing.
    """
    for line in lines:
        stripped = line.strip()
        if not stripped or FENCE.match(line) or ANY_HEADING.match(stripped):
            continue
        if any(stripped.startswith(label) for label in ONE_LINE_LABELS):
            continue
        return True
    return False


def check_scaffolding(body: str, report: Report) -> None:
    if any("<!--" in line for line in outside_lines(body)):
        report.error("body still contains an HTML comment: replace the template scaffolding with the answer and delete it.")


def check_sections(body: str, report: Report) -> dict[str, list[str]]:
    found = sections(body)
    for title in REQUIRED_SECTIONS:
        content = found.get(title.lower())
        if content is None:
            report.error(f"missing '## {title}' section.")
        elif not has_answer(content):
            report.error(f"'## {title}' is empty: answer it, or state why it does not apply.")
    return found


def check_verification(body: str, content: list[str] | None, root: str, report: Report) -> None:
    verified = label_values(body, "Verified commit:")
    not_checked = label_values(body, "Not checked:")
    for label, values, hint in (
        ("Verified commit:", verified, "name the full commit SHA the evidence was produced against."),
        ("Not checked:", not_checked, "say what you did not run and why, or that nothing was left out."),
    ):
        if len(values) != 1:
            report.error(f"'{label}' must appear once outside code blocks (found {len(values)}).")
        elif not values[0]:
            report.error(f"'{label}' is empty: {hint}")

    if content is not None and not has_answer(content):
        report.error("'## Verification' holds no command or result: an empty code block is not evidence.")

    if len(verified) != 1 or not verified[0]:
        return
    sha = verified[0]
    if not FULL_SHA.match(sha):
        report.error(f"'Verified commit: {sha}' must be the full 40-character SHA of the commit the evidence was produced against.")
        return
    inside_repo = subprocess.run(
        ["git", "-C", root, "rev-parse", "--is-inside-work-tree"],
        capture_output=True,
    )
    if inside_repo.returncode != 0:
        report.warn("not a git work tree: the 'Verified commit' SHA cannot be checked against the repository.")
        return
    exists = subprocess.run(
        ["git", "-C", root, "cat-file", "-e", f"{sha}^{{commit}}"],
        capture_output=True,
    )
    if exists.returncode != 0:
        report.error(f"'Verified commit: {sha}' names no commit in this repository.")


def build_report(body: str, root: str) -> Report:
    report = Report()
    check_scaffolding(body, report)
    found = check_sections(body, report)
    check_verification(body, found.get("verification"), root, report)
    return report


def emit(report: Report, summary_path: str | None) -> None:
    in_actions = os.environ.get("GITHUB_ACTIONS") == "true"
    for message in report.errors:
        print(f"::error title=PR body::{message}" if in_actions else f"[blocker] {message}")
    for message in report.warnings:
        print(f"::warning title=PR body::{message}" if in_actions else f"[warning] {message}")

    lines = [
        "## PR body structure",
        "",
        f"- blockers: **{len(report.errors)}**, warnings: **{len(report.warnings)}**",
        "",
    ]
    if report.errors:
        lines += ["### Blockers", ""] + [f"- {message}" for message in report.errors] + [""]
    if report.warnings:
        lines += ["### Warnings", ""] + [f"- {message}" for message in report.warnings] + [""]

    markdown = "\n".join(lines)
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as handle:
            handle.write(markdown)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--body-file", required=True, help="file containing the pull request body")
    parser.add_argument("--root", default=".", help="repository root used to check the verified commit (default: .)")
    args = parser.parse_args()

    body = open(args.body_file, encoding="utf-8").read()
    report = build_report(body, args.root)
    emit(report, os.environ.get("GITHUB_STEP_SUMMARY"))

    if report.errors:
        print(f"PR body invalid: {len(report.errors)} blocker(s).", file=sys.stderr)
        return 1
    print("PR body structure is valid.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
