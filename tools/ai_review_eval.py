#!/usr/bin/env python3
"""Evaluation of tools/ai_review.py on labelled diffs.

One case file = one diff + labels + runs (raw model responses or saved result
files). Output: TP / FP(known) / FP(unlabelled) / FN per run, plus a
micro-average over the whole set.

Labels are the external source of truth. Without them the harness measures the
repeatability of the filter, not the quality of the model - hence every case
must carry `labels` and `source` (where the labels came from).

A negative case (`"expect_silent": true`, empty `labels`) is a diff with no known
defect. Every finding in such a case counts as `fp_negative`: it is the only way
to measure precision, because a set built from fix commits only yields `real`
labels.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ai_review as ar  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def load_cases(path: Path) -> list[dict]:
    # An empty input must not end in a "TP=0 FP=0" report: it looks like a good
    # result and only means nothing was measured.
    if not path.exists():
        raise SystemExit(f"no cases: {path} does not exist")
    files = sorted(path.glob("*.json")) if path.is_dir() else [path]
    if not files:
        raise SystemExit(f"no cases: {path} contains no *.json")
    cases = [json.loads(f.read_text()) for f in files]
    for case in cases:
        missing = [k for k in ("id", "repo", "range", "head", "labels", "runs")
                   if k not in case]
        if missing:
            raise SystemExit(f"{case.get('id', '?')}: missing {', '.join(missing)}")
        if not case["labels"] and not case.get("expect_silent"):
            raise SystemExit(f"{case['id']}: empty labels would count every "
                             f"finding as FP; use expect_silent if that is intentional")
        bad = [l for l in case["labels"]
               if l.get("kind", "real") not in ("real", "fp", "unverified")]
        if bad:
            raise SystemExit(f"{case['id']}: unknown label kind in {bad[0]}")
    return cases


def _context(repo: Path, case: dict, prompt: str):
    intent = case.get("intent") or ar.commit_message(repo, case["head"])
    return ar.build_context(repo, case["range"], case["head"], intent,
                            case.get("max_changed_lines", 1500),
                            case.get("max_chars", 120000),
                            include_inventory=(prompt in ("v2", "v3", "v4")))


def run_findings(case: dict, run: dict) -> tuple[list[dict], int, dict | None]:
    """Findings of one run: from a raw response or from a result file."""
    if "result_file" in run:
        data = json.loads((ROOT / run["result_file"]).read_text())
        return data.get("kept", []), len(data.get("dropped", [])), None

    repo = (ROOT / case["repo"]).resolve()
    prompt = run.get("prompt", case.get("prompt", "v1"))
    _, meta = _context(repo, case, prompt)
    raw = (ROOT / run["response_file"]).read_text()
    parsed = ar.extract_json(raw)
    allowed = ar.allowed_invariants(
        repo, meta["invariants"],
        any(not (f["path"] or "").endswith(".md") for f in meta["parsed"]))
    kept, dropped = ar.verify(parsed.get("findings", []), meta["parsed"],
                              ar.test_inventory(repo), allowed)
    return ar.collapse(kept), len(dropped), meta


def score(findings: list[dict], labels: list[dict], tol: int,
          expect_silent: bool = False) -> dict:
    pools = {"real": [], "fp": [], "unverified": []}
    for label in labels:
        pools[label.get("kind", "real")].append(label)

    used: set[tuple[str, int]] = set()
    out = {"tp": [], "fp_known": [], "fp_unlabeled": [], "unverified": [],
           "fp_negative": []}
    for item in findings:
        path = ar._norm_path(item.get("_path") or "")
        line = item.get("_line")
        match = None
        for pool_name in ("real", "fp", "unverified"):
            for i, label in enumerate(pools[pool_name]):
                if (pool_name, i) in used:
                    continue
                if ar._norm_path(label["path"]) != path:
                    continue
                # `lines`: the defect region derived from the fix commit -
                # lines removed by the fix, mapped through `git blame fix^` onto
                # the reviewed commit. A hit anywhere in the region means the
                # same defect, so `tol` is not used here - it would widen the
                # region beyond what the fix actually changed.
                region = label.get("lines")
                if region is not None:
                    if line is not None and line not in region:
                        continue
                # `tol` per label: one defect can span several lines (e.g. the
                # same condition in 4 switch branches), and the label should mark
                # the region, not a single line.
                elif line is not None and label.get("line") is not None \
                        and abs(line - label["line"]) > label.get("tol", tol):
                    continue
                match = (pool_name, i)
                break
            if match:
                break
        if match is None:
            # A negative case has no label that could claim the finding, so every
            # hit is a candidate FP.
            out["fp_negative" if expect_silent else "fp_unlabeled"].append(item)
            continue
        used.add(match)
        out[{"real": "tp", "fp": "fp_known", "unverified": "unverified"}[match[0]]].append(item)

    out["fn"] = [label for i, label in enumerate(pools["real"])
                 if ("real", i) not in used]
    return out


def _loc(items: list[dict]) -> str:
    return ", ".join(f"{it.get('_path')}:{it.get('_line')}" for it in items)


def run_prompt(case: dict, run: dict) -> str | None:
    """The prompt that produced the run: from the run, the result file, or the case."""
    if "prompt" in run:
        return run["prompt"]
    if "result_file" in run:
        return json.loads((ROOT / run["result_file"]).read_text()).get("prompt")
    return case.get("prompt")


def eval_case(case: dict, tol: int, prompt_filter: str | None = None) -> dict:
    labels = case["labels"]
    runs, totals = [], {"tp": 0, "fp_known": 0, "fp_unlabeled": 0, "fn": 0,
                        "fp_negative": 0}
    for run in case["runs"]:
        if prompt_filter is not None and run_prompt(case, run) != prompt_filter:
            continue
        kept, dropped, meta = run_findings(case, run)
        result = score(kept, labels, tol, case.get("expect_silent", False))
        result["kept"] = len(kept)
        result["dropped"] = dropped
        result["payload_chars"] = meta["payload_chars"] if meta else None
        runs.append({"name": run["name"], **result})
        for key in totals:
            totals[key] += len(result[key])
    return {"id": case["id"], "labels": len(labels), "runs": runs, "totals": totals}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", default="tmp/ai-review-feedback/eval/cases",
                    help="directory of *.json case files or a single file")
    ap.add_argument("--out", default="tmp/ai-review-feedback/eval/results")
    ap.add_argument("--line-tolerance", type=int, default=3,
                    help="allowed line difference when matching a label")
    ap.add_argument("--prompt-filter", default=None,
                    help="count only runs produced by this prompt (v1/v2/v3/v4)")
    args = ap.parse_args()

    cases = load_cases(ROOT / args.cases)
    outdir = ROOT / args.out
    outdir.mkdir(parents=True, exist_ok=True)

    report, grand = [], {"tp": 0, "fp_known": 0, "fp_unlabeled": 0, "fn": 0,
                         "fp_negative": 0}
    print(f"cases: {len(cases)}, line tolerance: +-{args.line_tolerance}"
          + (f", prompt: {args.prompt_filter}" if args.prompt_filter else ""))
    for case in cases:
        result = eval_case(case, args.line_tolerance, args.prompt_filter)
        if not result["runs"]:
            continue
        report.append(result)
        for key in grand:
            grand[key] += result["totals"][key]
        print(f"\n{case['id']}  ({len(case['labels'])} labels, source: {case.get('source', '?')})")
        for run in result["runs"]:
            print(f"  {run['name']:<16} kept={run['kept']:<2} dropped={run['dropped']:<2} "
                  f"TP={len(run['tp'])} FP={len(run['fp_known'])}+{len(run['fp_unlabeled'])} "
                  f"FPneg={len(run['fp_negative'])} FN={len(run['fn'])}")
            if run["fn"]:
                print(f"      missed: {', '.join(l['path'] + ':' + str(l.get('line', '?')) for l in run['fn'])}")
            if run["fp_unlabeled"]:
                print(f"      unlabeled findings: {_loc(run['fp_unlabeled'])}")
            if run["fp_negative"]:
                print(f"      findings in negative case: {_loc(run['fp_negative'])}")

    tp, fp_known, fp_unl = grand["tp"], grand["fp_known"], grand["fp_unlabeled"]
    fp_neg = grand["fp_negative"]
    print(f"\nmicro (all runs): TP={tp} FP(labelled)={fp_known} "
          f"FP(negative)={fp_neg} FN={grand['fn']} findings without a label={fp_unl}")
    if tp + fp_known + fp_neg:
        print(f"  precision on labels {tp / (tp + fp_known + fp_neg):.2f}", end="")
    if tp + grand["fn"]:
        print(f", recall {tp / (tp + grand['fn']):.2f}")
    else:
        print()
    if fp_unl:
        print("  findings without a label are not counted as FP: there is no proof "
              "they are false - that is the cost of a label set built from fix commits")
    if grand["fn"]:
        print("  FN per case: " + ", ".join(
            f"{c['id']}={c['totals']['fn']}" for c in report if c["totals"]["fn"]))

    name = f"eval-{args.prompt_filter}.json" if args.prompt_filter else "eval.json"
    (outdir / name).write_text(json.dumps(
        {"line_tolerance": args.line_tolerance, "prompt_filter": args.prompt_filter,
         "totals": grand, "cases": report}, indent=2))
    print(f"-> {outdir / name}")


if __name__ == "__main__":
    main()
