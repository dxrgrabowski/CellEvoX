#!/usr/bin/env python3
"""Validate the machine-readable change contract at the top of a pull request body.

The contract is YAML front matter, described in .github/pull_request_template.md.
Scope is cross-checked against the real diff, so a pull request cannot silently
grow beyond what it declares. Deterministic: no model, no network, no guessing.

Usage:
    validate_pr_contract.py --body-file pr_body.md --base <sha> --head <sha>
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from typing import Any

REQUIRED_KEYS = ("intent", "risk", "scope")
KNOWN_KEYS = {"intent", "touches_invariants", "risk", "scope", "verification"}
RISK_LEVELS = ("low", "medium", "high")
# Paths where a "low" risk declaration is almost always a mistake. Warning only.
HIGH_BLAST_PREFIXES = ("CellEvoX/src/", "CellEvoX/include/", "web/backend/")
INVARIANT_NAME = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
# One-line answers the reviewer reads as contract fields, not prose.
ONE_LINE_FIELDS = (
    "Behavioral delta:",
    "Approach / rejected alternatives:",
    "Requirement source:",
    "Docs / ADR impact:",
    "Not checked:",
)
DOC_VALUES = ("none", "updated:", "override:")
VERIFIED_COMMIT = re.compile(r"^Verified commit:\s*[0-9a-f]{40}\s*$", re.MULTILINE)
AC_ROW = re.compile(r"^\|\s*(AC\d+)\s*\|(.*)\|\s*$", re.MULTILINE)
AC_NONE = re.compile(r"(^|\s)N/A\s+[—-]\s*\S")
SECTION_TITLE = re.compile(r"^##\s+(.+?)\s*$", re.MULTILINE)


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)

    def warn(self, message: str) -> None:
        self.warnings.append(message)


def split_front_matter(body: str) -> tuple[Any, str | None]:
    """Return (parsed_yaml, error). Body must start with a '---' fenced block."""
    if not body.lstrip().startswith("---"):
        return None, "PR body does not start with YAML front matter ('---'). Use .github/pull_request_template.md."
    lines = body.lstrip().splitlines()
    closing = next((i for i in range(1, len(lines)) if lines[i].strip() == "---"), None)
    if closing is None:
        return None, "unterminated YAML front matter: missing closing '---'."

    import yaml

    try:
        return yaml.safe_load("\n".join(lines[1:closing])), None
    except yaml.YAMLError as exc:
        return None, f"front matter is not valid YAML: {exc}"


def glob_to_regex(pattern: str) -> re.Pattern[str]:
    """Translate a repo-relative glob where '*' stops at '/' and '**' spans it."""
    out: list[str] = []
    i = 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("".join(out) + r"\Z")


def matches_any(path: str, patterns: list[str]) -> bool:
    return any(glob_to_regex(pattern).match(path) for pattern in patterns)


def changed_files(root: str, base: str, head: str) -> list[str]:
    # --no-renames keeps both sides of a rename visible to the scope check.
    result = subprocess.run(
        ["git", "-C", root, "diff", "--name-only", "--no-renames", f"{base}...{head}"],
        capture_output=True,
        text=True,
        check=True,
    )
    return [line for line in result.stdout.splitlines() if line.strip()]


def validate_contract(contract: Any, report: Report) -> tuple[list[str], list[str], str | None]:
    """Validate field shapes. Returns (scope.paths, must_not_change, risk)."""
    if not isinstance(contract, dict):
        report.error("front matter must be a YAML mapping, not a scalar or list.")
        return [], [], None

    for key in sorted(set(contract) - KNOWN_KEYS):
        report.warn(f"unknown contract key '{key}' (allowed: {', '.join(sorted(KNOWN_KEYS))}).")
    for key in REQUIRED_KEYS:
        if key not in contract:
            report.error(f"missing required key '{key}'.")

    intent = contract.get("intent")
    if not isinstance(intent, str) or not intent.strip():
        report.error("'intent' must be a non-empty string describing why this change exists.")

    risk = contract.get("risk")
    if risk not in RISK_LEVELS:
        report.error(f"'risk' must be one of {', '.join(RISK_LEVELS)} (got {risk!r}).")

    invariants = contract.get("touches_invariants", [])
    if not isinstance(invariants, list) or not all(isinstance(item, str) for item in invariants):
        report.error("'touches_invariants' must be a list of invariant names.")
    else:
        for name in invariants:
            if not INVARIANT_NAME.match(name):
                report.error(f"invariant '{name}' must be lowercase-kebab-case (e.g. snapshot-abi).")

    scope = contract.get("scope")
    paths: list[str] = []
    forbidden: list[str] = []
    if not isinstance(scope, dict):
        report.error("'scope' must be a mapping with 'paths' (and optionally 'must_not_change').")
    else:
        paths = scope.get("paths") or []
        forbidden = scope.get("must_not_change") or []
        if not isinstance(paths, list) or not all(isinstance(p, str) for p in paths):
            report.error("'scope.paths' must be a list of glob strings.")
            paths = []
        elif not paths:
            report.error("'scope.paths' is empty: declare at least one glob the diff must stay inside.")
        if not isinstance(forbidden, list) or not all(isinstance(p, str) for p in forbidden):
            report.error("'scope.must_not_change' must be a list of glob strings.")
            forbidden = []
        for pattern in patterns_of(paths) + patterns_of(forbidden):
            if pattern.startswith("/") or pattern.startswith("../"):
                report.error(f"scope glob '{pattern}' must be repo-relative (no leading '/' or '..').")

    if "verification" in contract:
        report.warn("'verification' moved out of the front matter: put commands and output in the 'Verification' section.")

    return paths, forbidden, risk if isinstance(risk, str) else None


def patterns_of(values: list[str]) -> list[str]:
    return [value for value in values if isinstance(value, str)]


def one_line(body: str, label: str) -> list[str]:
    """Every 'Label: value' line in the body, stripped, in document order."""
    pattern = rf"^{re.escape(label)}[ \t]*(.*)$"
    return [value.strip() for value in re.findall(pattern, body, re.MULTILINE)]


def check_body(body: str, report: Report, root: str) -> None:
    """Reject template scaffolding instead of letting a reviewer read it as intent."""
    if "<!--" in body:
        report.error("body still contains an HTML comment: delete the template scaffolding rather than leaving it to be interpreted.")

    for label in ONE_LINE_FIELDS:
        values = one_line(body, label)
        if len(values) != 1:
            report.error(f"'{label}' must appear once as a filled-in line (found {len(values)}).")
        elif not values[0]:
            report.error(f"'{label}' is empty: answer it, or write the answer that makes it true.")

    answers = one_line(body, "Docs / ADR impact:")
    if len(answers) == 1:
        answer = answers[0]
        prefix = next((value for value in DOC_VALUES if answer.lower().startswith(value)), None)
        if prefix is None:
            report.error(f"'Docs / ADR impact:' must start with one of {', '.join(DOC_VALUES)} (got '{answer}').")
        elif prefix != "none" and not answer[len(prefix):].strip(" :-"):
            report.error(f"'Docs / ADR impact: {prefix}' names nothing: add the paths or the ADR it refers to.")

    verified = VERIFIED_COMMIT.search(body)
    if not verified:
        report.error("'Verification' must name the full 40-character commit SHA it was produced against: 'Verified commit: <sha>'.")
    else:
        sha = re.search(r"[0-9a-f]{40}", verified.group(0)).group(0)
        exists = subprocess.run(
            ["git", "-C", root, "cat-file", "-e", f"{sha}^{{commit}}"],
            capture_output=True,
        )
        if exists.returncode != 0:
            report.error(f"'Verified commit: {sha}' is not a commit in this repository: name the commit the evidence was produced against.")


def section(body: str, title: str) -> str:
    """Return the text under '## <title>', up to the next level-2 heading."""
    match = re.search(rf"^##\s+{re.escape(title)}\s*$", body, re.MULTILINE)
    if not match:
        return ""
    rest = body[match.end():]
    end = SECTION_TITLE.search(rest)
    return rest[: end.start()] if end else rest


def check_criteria(body: str, report: Report) -> None:
    """The criteria are the contract; every one needs a block that verifies it."""
    criteria = section(body, "Acceptance Criteria")
    if not criteria:
        report.error("missing '## Acceptance Criteria' section.")
        return

    rows = AC_ROW.findall(criteria)
    ids = [ac for ac, _ in rows]
    duplicates = sorted({ac for ac in ids if ids.count(ac) > 1})
    if duplicates:
        report.error(f"duplicate acceptance criterion id(s): {', '.join(duplicates)}.")
    for ac, condition in rows:
        if not condition.strip(" -|"):
            report.error(f"{ac} has no observable condition: state what must be observably true, or drop the row.")
    if not ids and not AC_NONE.search(criteria):
        report.error("'Acceptance Criteria' has neither an 'AC<n>' row nor 'N/A — <reason>'.")

    verification = section(body, "Verification")
    if not verification:
        report.error("missing '## Verification' section.")
        return
    blocks = re.findall(r"^#{3,}\s+(.+?)\s*$\n?(.*?)(?=^#{3,}\s|\Z)", verification, re.MULTILINE | re.DOTALL)
    for ac in ids:
        headed = [content for heading, content in blocks if re.search(rf"\b{ac}\b", heading)]
        if not headed:
            report.error(f"no 'Verification' block for {ac}: head it '### {ac}' with the command and its decisive output.")
            continue
        # A block is proof only if it says more than the 'Not checked' disclaimer.
        has_output = any(re.sub(r"^Not checked:.*$", "", content, flags=re.MULTILINE).strip() for content in headed)
        if not has_output:
            report.error(f"the 'Verification' block for {ac} holds no command or output: a heading alone verifies nothing.")


def check_scope(files: list[str], paths: list[str], forbidden: list[str], report: Report) -> tuple[list[str], list[str]]:
    if not files:
        report.warn("diff is empty: nothing to check against scope.")
        return [], []

    undeclared = [path for path in files if paths and not matches_any(path, paths)]
    for path in undeclared:
        report.error(f"'{path}' is outside the declared scope.paths — widen the contract or drop the change.")

    touched_forbidden = [path for path in files if matches_any(path, forbidden)]
    for path in touched_forbidden:
        report.error(f"'{path}' matches scope.must_not_change — this PR promised not to touch it.")

    for pattern in paths:
        if not any(matches_any(path, [pattern]) for path in files):
            report.warn(f"scope.paths glob '{pattern}' matches nothing in this diff (stale declaration?).")

    return undeclared, touched_forbidden


def check_risk_heuristic(files: list[str], risk: str | None, report: Report) -> None:
    if risk != "low":
        return
    hot = [path for path in files if path.startswith(HIGH_BLAST_PREFIXES)]
    if hot:
        report.warn(f"declared risk 'low' but the diff touches {len(hot)} core path(s), e.g. '{hot[0]}'.")


def emit(report: Report, files: list[str], paths: list[str], summary_path: str | None) -> None:
    in_actions = os.environ.get("GITHUB_ACTIONS") == "true"
    for message in report.errors:
        print(f"::error title=PR contract::{message}" if in_actions else f"[blocker] {message}")
    for message in report.warnings:
        print(f"::warning title=PR contract::{message}" if in_actions else f"[warning] {message}")

    declared = [path for path in files if matches_any(path, paths)]
    lines = [
        "## PR contract",
        "",
        f"- changed files: **{len(files)}**",
        f"- covered by `scope.paths`: **{len(declared)}/{len(files)}**",
        f"- blockers: **{len(report.errors)}**, warnings: **{len(report.warnings)}**",
        "",
    ]
    if report.errors:
        lines += ["### Blockers", ""] + [f"- {message}" for message in report.errors] + [""]
    if report.warnings:
        lines += ["### Warnings", ""] + [f"- {message}" for message in report.warnings] + [""]
    if files:
        lines += ["### Changed files", "", "| declared | path |", "|---|---|"]
        lines += [f"| {'yes' if path in declared else 'no'} | `{path}` |" for path in files]
        lines.append("")

    markdown = "\n".join(lines)
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as handle:
            handle.write(markdown)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--body-file", required=True, help="file containing the pull request body")
    parser.add_argument("--base", help="base commit-ish for the diff")
    parser.add_argument("--head", help="head commit-ish for the diff")
    parser.add_argument("--root", default=".", help="repository root (default: .)")
    args = parser.parse_args()

    body = open(args.body_file, encoding="utf-8").read()
    report = Report()

    contract, parse_error = split_front_matter(body)
    if parse_error:
        report.error(parse_error)
        emit(report, [], [], os.environ.get("GITHUB_STEP_SUMMARY"))
        return 1

    paths, forbidden, risk = validate_contract(contract, report)
    check_body(body, report, args.root)
    check_criteria(body, report)

    files: list[str] = []
    if args.base and args.head:
        try:
            files = changed_files(args.root, args.base, args.head)
        except subprocess.CalledProcessError as exc:
            report.error(f"could not compute the diff against {args.base}: {exc.stderr.strip()}")
    else:
        report.warn("no --base/--head given: diff-vs-scope check skipped (contract only).")

    if files:
        check_scope(files, paths, forbidden, report)
        check_risk_heuristic(files, risk, report)

    emit(report, files, paths, os.environ.get("GITHUB_STEP_SUMMARY"))

    if report.errors:
        print(f"contract invalid: {len(report.errors)} blocker(s) in {len(files)} changed file(s).", file=sys.stderr)
        return 1
    print(f"contract valid: {len(files)} changed file(s) within declared scope.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
