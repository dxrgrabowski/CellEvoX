#!/usr/bin/env python3
"""Tests for `tools/review_bench.py`.

The fixtures reproduce the formats the three tools actually post: our sticky
comment (`#### 1. HIGH — `path:line``), Robin's review body
(`:rotating_light: **1 (`path:line`)** — ...`) and PR-Agent's reviewer guide with
its opt-in `Agent run details` block. Parsing those formats is the whole job of
the benchmark, so the tests pin them.

Run directly (no pytest dependency required):

    python3 tools/test_review_bench.py
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import review_bench as rb

OURS_BODY = """<!-- celeevox-ai-review -->
### AI diff review — `deepseek/deepseek-v4.1-flash-1017`, prompt `v4`

**3 findings** — 0 blockers, 3 warnings · 2 files · 41 changed lines

#### 1. HIGH — `web/backend/results_parser.py:57`
- **why:** legacy `stochastic: true` is mapped to `unknown`.

**evidence**

```diff
-    mode = "unknown"
+    mode = "stochastic"
```

#### 2. MEDIUM — `web/frontend/src/pages/ConfigPage.tsx:?`

**evidence**

```diff
+  alert(err.message)
```

#### 3. LOW — `CellEvoX/src/systems/SimulationEngine.cpp:120 (x3)`

**evidence**

```diff
-   f();
+   g();
```

<details><summary>Review context</summary>

- invariants in scope: `R1, R2`

</details>

<!-- celeevox-ai-review:usage {"model": "deepseek/deepseek-v4.1-flash-1017", \
"input_tokens": 12000, "output_tokens": 800} -->
"""

ROBIN_BODY = """:bow_and_arrow: Robin

## Code Review

:rotating_light: **2 High** | :warning: **1 Medium**

---

### :page_facing_up: Findings Not Posted Inline

:rotating_light: **1 (`CellEvoX/src/systems/SimulationEngine.cpp:42`)** — Brak sprawdzenia zwrotki z `init`.
> Sprawdź wynik przed dalszym użyciem.

:warning: **2 (`web/backend/results_parser.py`)** — Gałąź dla starszego configu nie jest pokryta.

:large_blue_circle: **3** — Rozważ zmianę nazwy.
"""

PRAGENT_BODY = """## PR Reviewer Guide 🔍

Here are some key observations to aid the review process:

<table>
<tr><td>⚡&nbsp;<strong>Recommended focus areas for review</strong><br><br>

<details><summary><a href='https://github.com/o/r/pull/1#diff-0f1e2d3c4b5aR55-R60'><strong>Possible Issue</strong></a>

The legacy branch maps `stochastic: true` to `unknown`, see
`web/backend/results_parser.py`.
</summary>

```python
mode = "unknown"
```

</details>

</td></tr>
</table>

<hr>
<details> <summary><strong>⚙️ Agent run details</strong></summary>

- Model: openrouter/deepseek/deepseek-v4.1-flash-1017
- Tokens: 12,345 in / 678 out / 13,023 total
- Time cost: 12.3s
- AI calls: 1
- Estimated API cost: $0.02 USD

</details>
"""


def _write_case(directory: str, **overrides) -> Path:
    case = {"id": "pr50", "repo": "owner/name", "pr": 50,
            "source": "test", "labels": [{"path": "a.py", "line": 10}]}
    case.update(overrides)
    path = Path(directory) / f"{case['id']}.json"
    path.write_text(json.dumps(case))
    return path


class ParseOursTest(unittest.TestCase):
    def test_reads_path_and_line(self):
        findings = rb._parse_ours(OURS_BODY)
        self.assertEqual(findings, [
            {"path": "web/backend/results_parser.py", "line": 57},
            {"path": "web/frontend/src/pages/ConfigPage.tsx", "line": None},
            {"path": "CellEvoX/src/systems/SimulationEngine.cpp", "line": 120},
        ])

    def test_ignores_prose_that_looks_like_a_finding(self):
        self.assertEqual(rb._parse_ours("#### 1. HIGH — not a location"), [])

    def test_reads_usage_marker(self):
        self.assertEqual(rb._usage_ours(OURS_BODY),
                         {"model": "deepseek/deepseek-v4.1-flash-1017",
                          "input_tokens": 12000, "output_tokens": 800})

    def test_no_usage_marker(self):
        self.assertIsNone(rb._usage_ours("### AI diff review\n"))

    def test_broken_usage_marker_is_not_a_crash(self):
        self.assertIsNone(rb._usage_ours("<!-- celeevox-ai-review:usage {nope} -->"))


class ParseRobinTest(unittest.TestCase):
    def test_reads_inline_comments(self):
        comments = [{"body": "**Robin** — problem", "path": "a.py", "line": 7},
                    {"body": "**Robin** — old", "path": "b.py", "line": None,
                     "original_line": 9},
                    {"body": "**Someone else** — not Robin", "path": "c.py", "line": 1},
                    {"body": "**Robin** — no path"}]
        self.assertEqual(rb._parse_robin_inline(comments),
                         [{"path": "a.py", "line": 7}, {"path": "b.py", "line": 9}])

    def test_reads_findings_left_in_the_review_body(self):
        self.assertEqual(rb._parse_robin_review(ROBIN_BODY), [
            {"path": "CellEvoX/src/systems/SimulationEngine.cpp", "line": 42},
            {"path": "web/backend/results_parser.py", "line": None},
        ])

    def test_counts_findings_without_a_file(self):
        self.assertEqual(rb._count_robin_unlocated(ROBIN_BODY), 1)


class ParsePragentTest(unittest.TestCase):
    def test_reads_key_issue_inline_comments(self):
        comments = [
            {"body": "<!-- pr-agent-key-issue-location: 0123456789ab -->\n"
                     "**Possible Issue**\n\nx", "path": "a.py", "line": 5},
            {"body": "<!-- pr-agent-dedup: 0123456789ab -->\nchatter", "path": "b.py",
             "line": 6},
        ]
        self.assertEqual(rb._parse_pragent_inline(comments),
                         [{"path": "a.py", "line": 5}])

    def test_falls_back_to_quoted_paths(self):
        # The `#diff-<sha256>` anchor does not contain the path, so the quoted
        # path is the only usable signal when inline key issues are off.
        self.assertEqual(rb._parse_pragent(PRAGENT_BODY),
                         [{"path": "web/backend/results_parser.py", "line": None}])

    def test_reads_tokens_and_cost(self):
        usage = rb._usage_pragent(PRAGENT_BODY)
        self.assertEqual(usage["input_tokens"], 12345)
        self.assertEqual(usage["output_tokens"], 678)
        self.assertEqual(usage["cost_usd"], 0.02)
        self.assertNotIn("cost_partial", usage)

    def test_cost_below_a_cent_is_not_zero(self):
        usage = rb._usage_pragent("- Estimated API cost: <$0.01 USD\n")
        self.assertEqual(usage, {"cost_usd": 0.005})

    def test_partial_cost_is_flagged(self):
        usage = rb._usage_pragent(
            "- Estimated API cost: $0.12 USD (partial: 1 of 2 successful calls priced)")
        self.assertEqual(usage["cost_usd"], 0.12)
        self.assertTrue(usage["cost_partial"])

    def test_unavailable_cost_is_flagged_and_not_priced(self):
        usage = rb._usage_pragent(
            "- Estimated API cost: unavailable (no calls could be priced)")
        self.assertNotIn("cost_usd", usage)
        self.assertTrue(usage["cost_unavailable"])

    def test_no_run_details_block(self):
        self.assertIsNone(rb._usage_pragent("## PR Reviewer Guide 🔍\n"))


class PriceTest(unittest.TestCase):
    def test_published_cost_wins_over_pricing_tokens(self):
        tool = {"usage": {"cost_usd": 0.02, "input_tokens": 1_000_000,
                          "output_tokens": 1_000_000}}
        self.assertEqual(rb._price(tool, 1.0, 1.0), 0.02)

    def test_tokens_are_priced_when_prices_are_given(self):
        tool = {"usage": {"input_tokens": 1_000_000, "output_tokens": 500_000}}
        self.assertEqual(rb._price(tool, 0.1, 0.4), 0.3)

    def test_tokens_without_prices_stay_unpriced(self):
        self.assertIsNone(rb._price({"usage": {"input_tokens": 10}}, 0.0, 0.0))

    def test_no_usage_stays_unpriced(self):
        self.assertIsNone(rb._price({"usage": None}, 1.0, 1.0))
        self.assertIsNone(rb._price({}, 1.0, 1.0))

    def test_money_never_prints_false_zero(self):
        self.assertEqual(rb._money(None), "unpriced")
        self.assertEqual(rb._money(0.005), "$0.0050")
        self.assertEqual(rb._money(0.02), "$0.02")


class MetricsTest(unittest.TestCase):
    def test_matches_a_label_within_tolerance(self):
        labels = [{"path": "a.py", "line": 10, "kind": "real"}]
        metrics = rb._metrics([{"path": "a.py", "line": 12}], labels, 3, False)
        self.assertEqual((metrics["tp"], metrics["fp"], metrics["fn"]), (1, 0, 0))
        self.assertEqual(metrics["located"], 1)

    def test_counts_a_finding_outside_the_label_as_false_positive(self):
        labels = [{"path": "a.py", "line": 10, "kind": "real"}]
        metrics = rb._metrics([{"path": "a.py", "line": 40}], labels, 3, False)
        self.assertEqual((metrics["tp"], metrics["fp"], metrics["fn"]), (0, 1, 1))
        self.assertEqual(metrics["false_positives"], ["a.py:40"])
        self.assertEqual(metrics["missed"], ["a.py:10"])

    def test_region_label_matches_any_line_inside(self):
        labels = [{"path": "a.py", "lines": [5, 6, 7], "kind": "real"}]
        self.assertEqual(rb._metrics([{"path": "a.py", "line": 6}], labels, 3,
                                     False)["tp"], 1)
        self.assertEqual(rb._metrics([{"path": "a.py", "line": 9}], labels, 3,
                                     False)["tp"], 0)

    def test_negative_case_counts_every_finding_as_false_positive(self):
        metrics = rb._metrics([{"path": "a.py", "line": 1}], [], 3, True)
        self.assertEqual((metrics["tp"], metrics["fp"]), (0, 1))

    def test_counts_findings_the_tool_reported_without_a_location(self):
        metrics = rb._metrics([{"path": "a.py", "line": None}],
                              [{"path": "a.py", "line": 1, "kind": "real"}],
                              3, False, unlocated=2)
        self.assertEqual(metrics["located"], 0)
        self.assertEqual(metrics["unlocated"], 2)


class CollectTest(unittest.TestCase):
    INLINE = [
        {"body": "**Robin** — problem", "path": "a.py", "line": 5},
        {"body": "<!-- pr-agent-key-issue-location: 0123456789ab -->\n"
                 "**Possible Issue**\n\nx", "path": "web/backend/results_parser.py",
         "line": 57},
    ]
    REVIEWS = [{"body": ROBIN_BODY}, {"body": PRAGENT_BODY}]
    COMMENTS = [{"body": OURS_BODY}]

    def _collect(self):
        def fake_paginate(path, token):
            if path.endswith("/comments") and "/pulls/" in path:
                return self.INLINE
            if path.endswith("/reviews"):
                return self.REVIEWS
            if path.endswith("/comments"):
                return self.COMMENTS
            raise AssertionError(f"unexpected endpoint {path}")
        with mock.patch.object(rb, "_paginate", fake_paginate):
            return rb.collect("owner/name", 50, "token")

    def test_recognises_all_three_tools(self):
        self.assertEqual(sorted(self._collect()["tools"]),
                         ["ours", "pr-agent", "robin"])

    def test_robin_scores_inline_and_body_findings(self):
        robin = self._collect()["tools"]["robin"]
        self.assertEqual([f["path"] for f in robin["findings"]],
                         ["a.py", "CellEvoX/src/systems/SimulationEngine.cpp",
                          "web/backend/results_parser.py"])
        self.assertEqual(robin["unlocated"], 1)
        self.assertIsNone(robin["usage"])

    def test_pragent_prefers_inline_key_issues(self):
        pragent = self._collect()["tools"]["pr-agent"]
        self.assertEqual(pragent["findings"],
                         [{"path": "web/backend/results_parser.py", "line": 57}])
        self.assertEqual(pragent["usage"]["cost_usd"], 0.02)

    def test_pragent_counts_summary_entries_that_are_not_inline(self):
        reviews = [{"body": PRAGENT_BODY.replace("</details>\n\n</td>",
                                                 "</details>\n\n<details><summary>"
                                                 "<strong>Other</strong></summary>\n\nx\n\n"
                                                 "</details>\n\n</td>")}]
        with mock.patch.object(self, "REVIEWS", reviews):
            pragent = self._collect()["tools"]["pr-agent"]
        self.assertEqual(len(pragent["findings"]), 1)
        self.assertEqual(pragent["unlocated"], 1)

    def test_missing_tool_is_absent_instead_of_empty(self):
        with mock.patch.object(self, "COMMENTS", []):
            tools = self._collect()["tools"]
        self.assertNotIn("ours", tools)
        self.assertIn("robin", tools)


class RenderTest(unittest.TestCase):
    def _result(self, tools):
        return {"case": "pr50", "repo": "owner/name", "pr": 50, "labels": [{}],
                "expect_silent": False, "tools": tools}

    def test_prints_every_tool_and_marks_missing_ones(self):
        report = rb.render(self._result({
            "ours": {"metrics": {"tp": 2, "fp": 0, "fn": 1, "findings": 2,
                                 "located": 2, "unlocated": 0,
                                 "false_positives": [], "missed": []},
                     "cost_usd": 0.02},
        }))
        self.assertIn("| `ours` | 2 | 0 | 1 | 1.00 | 0.67 |", report)
        self.assertIn("$0.01", report)          # $/TP
        self.assertIn("| `robin` | – | – | – | – | – | – | – | – | not run | – |",
                      report)
        self.assertIn("unpriced", report)

    def test_unpriced_tool_has_no_cost_per_true_positive(self):
        report = rb.render(self._result({
            "robin": {"metrics": {"tp": 1, "fp": 0, "fn": 0, "findings": 1,
                                  "located": 1, "unlocated": 3,
                                  "false_positives": [], "missed": []},
                      "cost_usd": None},
        }))
        self.assertIn("| `robin` | 1 | 0 | 0 | 1.00 | 1.00 | 1 | 1/1 | 3 | unpriced | "
                      "unpriced |", report)

    def test_lists_mismatches(self):
        report = rb.render(self._result({
            "ours": {"metrics": {"tp": 0, "fp": 1, "fn": 1, "findings": 1,
                                 "located": 1, "unlocated": 0,
                                 "false_positives": ["a.py:40"],
                                 "missed": ["a.py:10"]},
                     "cost_usd": None},
        }))
        self.assertIn("`ours` false positives: a.py:40", report)
        self.assertIn("`ours` missed labels: a.py:10", report)


class CaseTest(unittest.TestCase):
    def test_loads_a_valid_case(self):
        with tempfile.TemporaryDirectory() as tmp:
            case = rb._load_case(_write_case(tmp))
        self.assertEqual(case["id"], "pr50")

    def test_rejects_a_repo_that_is_not_owner_name(self):
        # The eval cases use a worktree path as `repo`; the benchmark needs the
        # GitHub name instead, and silently accepting the path would score a
        # different repository.
        with tempfile.TemporaryDirectory() as tmp:
            path = _write_case(tmp, repo=".issue48-worktree")
            with self.assertRaises(SystemExit):
                rb._load_case(path)

    def test_rejects_a_case_without_labels(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = _write_case(tmp, labels=[])
            with self.assertRaises(SystemExit):
                rb._load_case(path)

    def test_accepts_an_empty_negative_case(self):
        with tempfile.TemporaryDirectory() as tmp:
            case = rb._load_case(_write_case(tmp, labels=[], expect_silent=True))
        self.assertEqual(case["labels"], [])

    def test_reports_a_missing_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = _write_case(tmp)
            path.write_text(json.dumps({"id": "pr50", "repo": "owner/name"}))
            with self.assertRaises(SystemExit):
                rb._load_case(path)

    def test_finds_the_case_filed_for_a_pr(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write_case(tmp, id="other", pr=51)
            _write_case(tmp, id="pr50", pr=50)
            with mock.patch.object(rb, "CASES", Path(tmp)):
                self.assertEqual(rb._find_case(50)["id"], "pr50")
                self.assertIsNone(rb._find_case(52))


if __name__ == "__main__":
    unittest.main()
