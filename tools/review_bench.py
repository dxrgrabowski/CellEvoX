#!/usr/bin/env python3
"""Side-by-side benchmark of the review bots that comment on this repository.

Three bots review the same pull requests: our own `tools/ai_review.py`, Robin
(https://github.com/antongulin/robin) and Qodo PR-Agent. This script collects
what they posted on one PR, normalises the three comment formats into located
findings (a path, and a line when the tool reports one), and scores them
against the labels of a case in `bench/cases/`.

It deliberately does not run the bots. Robin and PR-Agent are GitHub
integrations: their supported entry point is a pull request and their product is
the review they post, so driving them offline would measure a different
program. The benchmark therefore measures the deployment, not the prompt.

Scoring is `ai_review_eval.score`, so a label means the same thing here as in
the local eval: a path plus a `lines` region, a `line` with a tolerance, or a
path-only match for tools that only report files.

Cost is measured, never assumed. A tool is priced only from what it publishes
itself (a token count or a cost); with no published usage the tool is
`unpriced` and no $/TP figure is produced. PR-Agent publishes tokens and an
estimated cost when its workflow enables `config.output_run_details` and
`config.output_run_cost`; Robin publishes neither. Pass `--price-in/--price-out`
(USD per million tokens) to turn published tokens into dollars.

Usage:
    python3 tools/review_bench.py case --pr 50
    python3 tools/review_bench.py collect --pr 50 --case bench/cases/pr50.json
    python3 tools/review_bench.py report --results bench/results
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ai_review_eval import score as score_findings  # noqa: E402

API = "https://api.github.com"
ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "bench" / "cases"
TOOLS = ("ours", "robin", "pr-agent")

# How each tool is recognised in the PR thread. These strings are part of each
# tool's published output, so a format change shows up as a missing tool in the
# report instead of silently scoring zero findings.
OURS_MARKER = "<!-- celeevox-ai-review -->"
ROBIN_BODY_MARKER = ":bow_and_arrow: Robin"
ROBIN_INLINE_PREFIX = "**Robin**"
# PR-Agent moved from a visible header to a hidden identity marker; both are
# accepted so an older comment still scores.
PRAGENT_MARKERS = (
    "<!-- pr-agent:review:full -->",
    "<!-- pr-agent:review:incremental -->",
    "## PR Reviewer Guide",
    "## Incremental PR Reviewer Guide",
)
# Inline review comment carrying one key issue; the location marker is what
# separates it from the tool's other inline comments.
PRAGENT_KEY_ISSUE_MARKER = "<!-- pr-agent-key-issue-location:"

USAGE_RE = re.compile(r"<!-- celeevox-ai-review:usage (\{.*?\}) -->", re.S)
OURS_FINDING_RE = re.compile(
    r"^#### \d+\. [A-Za-z]+ — `([^`]+?):(\d+|\?)(?: \(x(\d+)\))?`$", re.M)
# Robin prints findings it did not attach inline as
# `:rotating_light: **3 (`path/to/file.py:42`)** — description`, and without the
# `:line` when the model reported no line. Findings with no file at all carry no
# location, so they can only be counted, not scored.
ROBIN_FINDING_RE = re.compile(r"^:[\w-]+: \*\*\d+ \(`([^`]+?)(?::(\d+))?`\)\*\*", re.M)
ROBIN_UNLOCATED_RE = re.compile(r"^:[\w-]+: \*\*\d+\*\* — ", re.M)
PATH_RE = re.compile(
    r"`([A-Za-z0-9_][A-Za-z0-9_./-]*\.(?:cpp|cc|cxx|h|hpp|hxx|py|md|yml|yaml|"
    r"toml|json|txt|cmake|sh|in|cfg))`(?::(\d+))?")
TOKENS_LINE_RE = re.compile(r"^-\s*Tokens:\s*(.+)$", re.M)
TOKEN_PART_RE = re.compile(r"([\d,]+)\s*(in|out|total)\b")
COST_LINE_RE = re.compile(r"^-\s*Estimated API cost:\s*(.+)$", re.M)


def _token(explicit: str | None) -> str:
    if explicit:
        return explicit
    for name in ("GITHUB_TOKEN", "GH_TOKEN"):
        if os.environ.get(name):
            return os.environ[name]
    raise SystemExit("no GitHub token: set GITHUB_TOKEN/GH_TOKEN or pass --token")


def _get(path: str, token: str, page: int) -> list:
    url = f"{API}{path}{'&' if '?' in path else '?'}per_page=100&page={page}"
    request = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "celleevox-review-bench",
    })
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read())
    except urllib.error.HTTPError as exc:
        raise SystemExit(f"GET {path} failed: {exc.code} {exc.reason}") from exc


def _paginate(path: str, token: str) -> list:
    items, page = [], 1
    while True:
        batch = _get(path, token, page)
        items.extend(batch)
        if len(batch) < 100:
            return items
        page += 1


def _bodies(entries: list, key: str = "body") -> list[str]:
    return [(entry.get(key) or "") for entry in entries if entry.get(key)]


def _parse_ours(text: str) -> list[dict]:
    findings = []
    for match in OURS_FINDING_RE.finditer(text):
        line = match.group(2)
        findings.append({"path": match.group(1),
                         "line": None if line == "?" else int(line)})
    return findings


def _parse_robin_inline(comments: list[dict]) -> list[dict]:
    findings = []
    for comment in comments:
        if not (comment.get("body") or "").startswith(ROBIN_INLINE_PREFIX):
            continue
        if not comment.get("path"):
            continue
        findings.append({"path": comment["path"],
                         "line": comment.get("line") or comment.get("original_line")})
    return findings


def _parse_robin_review(text: str) -> list[dict]:
    """Findings Robin printed in the review body instead of attaching inline."""
    return [{"path": m.group(1), "line": int(m.group(2)) if m.group(2) else None}
            for m in ROBIN_FINDING_RE.finditer(text)]


def _count_robin_unlocated(text: str) -> int:
    return len(ROBIN_UNLOCATED_RE.findall(text))


def _parse_pragent_inline(comments: list[dict]) -> list[dict]:
    """Key issues PR-Agent published as inline comments (path and line given)."""
    findings = []
    for comment in comments:
        if PRAGENT_KEY_ISSUE_MARKER not in (comment.get("body") or ""):
            continue
        if not comment.get("path"):
            continue
        findings.append({"path": comment["path"],
                         "line": comment.get("line") or comment.get("original_line")})
    return findings


def _parse_pragent(text: str) -> list[dict]:
    """Fallback: file paths quoted in the reviewer guide (no line numbers).

    PR-Agent links each finding to a `#diff-<sha256>R<start>-R<end>` anchor, so
    the path cannot be recovered from the link; the quoted path is the only
    usable signal when inline key issues are disabled.
    """
    seen, findings = set(), []
    for match in PATH_RE.finditer(text):
        line = int(match.group(2)) if match.group(2) else None
        key = (match.group(1), line)
        if key not in seen:
            seen.add(key)
            findings.append({"path": match.group(1), "line": line})
    return findings


def _number(raw: str) -> int:
    return int(raw.replace(",", "").replace(" ", ""))


def _usage_ours(text: str) -> dict | None:
    match = USAGE_RE.search(text)
    if not match:
        return None
    try:
        return json.loads(match.group(1))
    except json.JSONDecodeError:
        return None


def _usage_pragent(text: str) -> dict | None:
    """The opt-in `Agent run details` block: token counts and an estimated cost.

    A run that published neither is unpriced, not free - `usage` stays empty and
    `_price` returns None instead of 0.
    """
    usage: dict = {}
    tokens = TOKENS_LINE_RE.search(text)
    if tokens:
        for value, label in TOKEN_PART_RE.findall(tokens.group(1)):
            if label == "in":
                usage["input_tokens"] = _number(value)
            elif label == "out":
                usage["output_tokens"] = _number(value)
    cost = COST_LINE_RE.search(text)
    if cost:
        amount = cost.group(1).strip()
        if amount.startswith("<"):
            # `<$0.01` is a positive amount rounded away; 0 would be a lie and
            # 0.01 would overstate it.
            usage["cost_usd"] = 0.005
        else:
            match = re.match(r"\$([\d.]+)", amount)
            if match:
                usage["cost_usd"] = float(match.group(1))
        if "partial" in amount:
            usage["cost_partial"] = True
        elif "unavailable" in amount:
            usage["cost_unavailable"] = True
    return usage or None


def collect(repo: str, pr: int, token: str) -> dict:
    # Three endpoints because the tools speak in three different places: our
    # comment is an issue comment, Robin and PR-Agent attach inline review
    # comments, and PR-Agent also posts a review.
    inline = _paginate(f"/repos/{repo}/pulls/{pr}/comments", token)
    reviews = _paginate(f"/repos/{repo}/pulls/{pr}/reviews", token)
    comments = _paginate(f"/repos/{repo}/issues/{pr}/comments", token)
    review_bodies = _bodies(reviews)
    comment_bodies = _bodies(comments)

    tools: dict[str, dict] = {}

    ours_bodies = [b for b in comment_bodies if OURS_MARKER in b]
    if ours_bodies:
        body = ours_bodies[-1]
        tools["ours"] = {"findings": _parse_ours(body),
                         "unlocated": 0,
                         "usage": _usage_ours(body)}

    robin_bodies = ([b for b in review_bodies if ROBIN_BODY_MARKER in b]
                    + [b for b in comment_bodies if ROBIN_BODY_MARKER in b])
    robin_inline = _parse_robin_inline(inline)
    if robin_bodies or robin_inline:
        findings = list(robin_inline)
        unlocated = 0
        for body in robin_bodies:
            findings.extend(_parse_robin_review(body))
            unlocated += _count_robin_unlocated(body)
        tools["robin"] = {"findings": findings, "unlocated": unlocated,
                          "usage": None}

    pragent_bodies = [b for b in review_bodies + comment_bodies
                      if any(marker in b for marker in PRAGENT_MARKERS)]
    if pragent_bodies:
        body = pragent_bodies[-1]
        inline_findings = _parse_pragent_inline(inline)
        if inline_findings:
            # Counting the summary entries shows how many findings were reported
            # but could not be published inline (and therefore cannot be scored).
            reported = body.count("<details><summary>")
            findings = inline_findings
            unlocated = max(0, reported - len(inline_findings))
        else:
            findings = _parse_pragent(body)
            unlocated = 0
        tools["pr-agent"] = {"findings": findings, "unlocated": unlocated,
                             "usage": _usage_pragent(body)}
    return {"pr": pr, "repo": repo, "tools": tools}


def _metrics(findings: list[dict], labels: list[dict], tol: int,
             expect_silent: bool, unlocated: int = 0) -> dict:
    scored = score_findings(
        [{"_path": f["path"], "_line": f.get("line")} for f in findings],
        labels, tol, expect_silent)
    tp = len(scored["tp"])
    fp = (len(scored["fp_unlabeled"]) + len(scored["fp_known"])
          + len(scored["fp_negative"]))
    fn = len(scored["fn"])
    return {"tp": tp, "fp": fp, "fn": fn,
            "findings": len(findings),
            "located": sum(1 for f in findings if f.get("line") is not None),
            "unlocated": unlocated,
            # `score` hands back the items it was given, i.e. `_path`/`_line`.
            "false_positives": [f"{f['_path']}:{f['_line'] if f['_line'] is not None else '?'}"
                                for f in scored["fp_unlabeled"] + scored["fp_known"]
                                + scored["fp_negative"]][:10],
            "missed": [f"{l['path']}:{l.get('line') or '?'}" for l in scored["fn"]][:10]}


def _price(tool: dict, price_in: float, price_out: float) -> float | None:
    usage = tool.get("usage") or {}
    if not usage:
        return None
    if usage.get("cost_usd") is not None:
        return float(usage["cost_usd"])
    if not price_in and not price_out:
        return None
    return (float(usage.get("input_tokens") or 0) * price_in
            + float(usage.get("output_tokens") or 0) * price_out) / 1_000_000


def _ratio(numerator: float, denominator: float) -> str:
    return "n/a" if not denominator else f"{numerator / denominator:.2f}"


def _money(value: float | None) -> str:
    if value is None:
        return "unpriced"
    return f"${value:.4f}" if value < 0.01 else f"${value:.2f}"


def render(result: dict) -> str:
    labels = result["labels"]
    expect_silent = result.get("expect_silent", False)
    out = [f"### Review benchmark — case `{result['case']}` (PR #{result['pr']})",
           "",
           f"{len(labels)} label{'' if len(labels) == 1 else 's'}"
           + (" · negative case: every finding is a false positive" if expect_silent else "")
           + f" · {result['repo']}",
           "",
           "| tool | TP | FP | FN | precision | recall | findings | line-anchored | "
           "unlocated | cost | $/TP |",
           "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for name in TOOLS:
        tool = result["tools"].get(name)
        if tool is None:
            out.append(f"| `{name}` | – | – | – | – | – | – | – | – | not run | – |")
            continue
        metrics = tool["metrics"]
        cost = tool.get("cost_usd")
        per_tp = None if cost is None or not metrics["tp"] else cost / metrics["tp"]
        out.append("| `{}` | {} | {} | {} | {} | {} | {} | {}/{} | {} | {} | {} |".format(
            name, metrics["tp"], metrics["fp"], metrics["fn"],
            _ratio(metrics["tp"], metrics["tp"] + metrics["fp"]),
            _ratio(metrics["tp"], metrics["tp"] + metrics["fn"]),
            metrics["findings"], metrics["located"], metrics["findings"],
            metrics["unlocated"], _money(cost), _money(per_tp)))

    details = []
    for name in TOOLS:
        tool = result["tools"].get(name)
        if not tool:
            continue
        metrics = tool["metrics"]
        if metrics["false_positives"]:
            details.append(f"- `{name}` false positives: "
                           + ", ".join(metrics["false_positives"]))
        if metrics["missed"]:
            details.append(f"- `{name}` missed labels: " + ", ".join(metrics["missed"]))
    if details:
        out += ["", "<details><summary>Mismatches</summary>", "", *details, "",
                "</details>"]
    out += ["", "_Cost is what a tool publishes about its own run. PR-Agent reports "
            "tokens and an estimated cost only when its workflow enables "
            "`config.output_run_details` and `config.output_run_cost`; Robin reports "
            "neither, so it stays unpriced; ours publishes a token marker, which "
            "`--price-in/--price-out` turns into dollars. No `$0` is ever assumed. "
            "`unlocated` findings are ones the tool reported without a path, so they "
            "cannot be scored._"]
    return "\n".join(out)


def _load_case(path: Path) -> dict:
    if not path.exists():
        raise SystemExit(f"no such case: {path}")
    case = json.loads(path.read_text())
    missing = [key for key in ("id", "repo", "pr", "labels", "source")
               if key not in case]
    if missing:
        raise SystemExit(f"{path.name}: missing {', '.join(missing)}")
    if "/" not in case["repo"]:
        raise SystemExit(f"{case['id']}: repo must be owner/name, "
                         f"got {case['repo']!r}")
    # Same rule as ai_review_eval: an empty label list must be declared, because
    # otherwise every finding silently counts as a false positive.
    if not case["labels"] and not case.get("expect_silent"):
        raise SystemExit(f"{case['id']}: empty labels - use expect_silent for a "
                         f"negative case")
    return case


def _case_path(name: str) -> Path:
    path = Path(name)
    if path.suffix != ".json":
        path = path.with_suffix(".json")
    if path.exists():
        return path
    if (CASES / path.name).exists():
        return CASES / path.name
    raise SystemExit(f"no such case: {name}")


def _find_case(pr: int, repo: str | None = None) -> dict | None:
    """The case filed for this PR; a case is what makes a PR measurable."""
    for path in sorted(CASES.glob("*.json")):
        case = json.loads(path.read_text())
        if case.get("pr") != pr:
            continue
        if repo and case.get("repo", "").lower() != repo.lower():
            continue
        return case
    return None


def command_case(args) -> int:
    case = _find_case(args.pr, args.repo)
    if case is None:
        print(f"no case for PR #{args.pr} in {CASES}", file=sys.stderr)
        return 1
    print(case["id"])
    return 0


def command_collect(args) -> int:
    if args.case:
        case = _load_case(_case_path(args.case))
    elif args.pr is not None:
        case = _find_case(args.pr, args.repo)
        if case is None:
            raise SystemExit(f"no case for PR #{args.pr} in {CASES}; add one first")
    else:
        raise SystemExit("pass --pr (with a matching case) or --case")

    pr = args.pr or case["pr"]
    repo = args.repo or case["repo"]
    collected = collect(repo, pr, _token(args.token))
    for tool in collected["tools"].values():
        tool["metrics"] = _metrics(tool["findings"], case["labels"], args.tol,
                                   case.get("expect_silent", False),
                                   tool.get("unlocated", 0))
        tool["cost_usd"] = _price(tool, args.price_in, args.price_out)

    result = {"case": case["id"], "case_source": case["source"],
              "repo": collected["repo"], "pr": pr,
              "labels": case["labels"],
              "expect_silent": case.get("expect_silent", False),
              "tools": collected["tools"]}

    report = render(result)
    print(report)
    if args.out:
        outdir = Path(args.out)
        outdir.mkdir(parents=True, exist_ok=True)
        path = outdir / f"{case['id']}-pr{pr}.json"
        path.write_text(json.dumps(result, indent=2))
        print(f"\nwrote {path}", file=sys.stderr)
    if not collected["tools"]:
        print("no review comments found: did the bots run on this PR?", file=sys.stderr)
        return 1
    return 0


def command_report(args) -> int:
    files = sorted(Path(args.results).glob("*.json")) \
        if Path(args.results).is_dir() else [Path(args.results)]
    if not files:
        raise SystemExit(f"no results in {args.results}")
    totals: dict[str, dict] = {}
    for path in files:
        for name, tool in json.loads(path.read_text())["tools"].items():
            bucket = totals.setdefault(name, {"tp": 0, "fp": 0, "fn": 0, "cost": 0.0,
                                              "priced": True, "prs": 0})
            bucket["prs"] += 1
            for key in ("tp", "fp", "fn"):
                bucket[key] += tool["metrics"][key]
            if tool.get("cost_usd") is None:
                bucket["priced"] = False
            else:
                bucket["cost"] += tool["cost_usd"]

    out = [f"### Review benchmark — {len(files)} case(s)", "",
           "| tool | PRs | TP | FP | FN | precision | recall | cost | $/TP |",
           "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for name in TOOLS:
        bucket = totals.get(name)
        if not bucket:
            continue
        cost = bucket["cost"] if bucket["priced"] else None
        per_tp = None if cost is None or not bucket["tp"] else cost / bucket["tp"]
        out.append(f"| `{name}` | {bucket['prs']} | {bucket['tp']} | {bucket['fp']} | "
                   f"{bucket['fn']} | {_ratio(bucket['tp'], bucket['tp'] + bucket['fp'])} | "
                   f"{_ratio(bucket['tp'], bucket['tp'] + bucket['fn'])} | "
                   f"{_money(cost)} | {_money(per_tp)} |")
    print("\n".join(out))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    case_parser = sub.add_parser("case", help="print the case filed for a PR")
    case_parser.add_argument("--pr", type=int, required=True)
    case_parser.add_argument("--repo", default=None, help="owner/name filter")
    case_parser.set_defaults(func=command_case)

    collect_parser = sub.add_parser("collect", help="score one PR")
    collect_parser.add_argument("--pr", type=int, default=None,
                                help="PR number; defaults to the case's PR")
    collect_parser.add_argument("--case", default=None,
                                help="case id or path; defaults to the case filed "
                                     "for --pr")
    collect_parser.add_argument("--repo", default=None,
                                help="owner/name; defaults to the case's repo")
    collect_parser.add_argument("--token", default=None)
    collect_parser.add_argument("--tol", type=int, default=3,
                                help="line tolerance for labels without `lines`")
    collect_parser.add_argument("--price-in", type=float, default=0.0,
                                help="USD per million input tokens")
    collect_parser.add_argument("--price-out", type=float, default=0.0,
                                help="USD per million output tokens")
    collect_parser.add_argument("--out", default=str(ROOT / "bench" / "results"),
                                help="directory for the JSON result")
    collect_parser.set_defaults(func=command_collect)

    report_parser = sub.add_parser("report", help="aggregate saved results")
    report_parser.add_argument("--results", default=str(ROOT / "bench" / "results"))
    report_parser.set_defaults(func=command_report)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
