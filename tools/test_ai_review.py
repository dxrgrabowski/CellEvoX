"""Tests for the advisory review tool (tools/ai_review.py).

No network and no model call: the response is supplied through --response-file,
so what is tested is the deterministic half - diff parsing, quote verification,
context assembly and the markdown that becomes the PR comment.
"""

from __future__ import annotations

import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

import pytest

TOOL = Path(__file__).with_name("ai_review.py")


def _load_tool():
    spec = importlib.util.spec_from_file_location("ai_review", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ai = _load_tool()


DIFF = """diff --git a/x.txt b/x.txt
index 1111111..2222222 100644
--- a/x.txt
+++ b/x.txt
@@ -1,3 +1,3 @@
 keep
--- old flag
+++ new flag
 keep2
diff --git a/gone.txt b/gone.txt
deleted file mode 100644
--- a/gone.txt
+++ /dev/null
@@ -1 +0,0 @@
-bye
"""

AGENTS_MD = """\
# AGENTS

## Hard Domain Invariants

1. **Deterministic replay** - same seed, same output.
2. **Thread-count independence** - results do not depend on thread count.

## Negative Space

- Anything that changes the snapshot ABI needs an ADR.

## Other
"""


def _git(repo: Path, *args: str) -> None:
    subprocess.run(["git", "-C", str(repo), *args], check=True,
                   capture_output=True, text=True)


def _make_repo(tmp_path: Path) -> Path:
    repo = tmp_path / "repo"
    repo.mkdir()
    _git(repo, "init", "-q")
    _git(repo, "config", "user.email", "test@example.com")
    _git(repo, "config", "user.name", "Test")
    (repo / "AGENTS.md").write_text(AGENTS_MD)
    (repo / "CellEvoX/tests").mkdir(parents=True)
    (repo / "CellEvoX/tests/test_systems.cpp").write_text(
        'TEST_CASE("deterministic replay", "[systems]")\n'
        'TEST_CASE("population growth")\n')
    source = repo / "CellEvoX/src/systems/engine.cpp"
    source.parent.mkdir(parents=True)
    source.write_text("int step() {\n    return 1;\n}\n")
    _git(repo, "add", "-A")
    _git(repo, "commit", "-q", "-m", "base")
    source.write_text("int step() {\n    return 2;\n}\n")
    _git(repo, "commit", "-q", "-am", "change the step result")
    return repo


# ---------------------------------------------------------------- diff parsing

def test_parse_diff_numbers_both_sides():
    files = ai.parse_diff(DIFF)
    assert [f["path"] for f in files] == ["x.txt", "gone.txt"]
    first = files[0]
    assert [line[0] for line in first["lines"]] == ["H", "C", "D", "A", "C"]
    removed, added = first["lines"][2], first["lines"][3]
    assert removed[2] == 2 and removed[3] is None
    assert added[2] is None and added[3] == 2
    assert files[1]["deleted"] is True


def test_parse_diff_does_not_read_hunk_lines_as_file_headers():
    # A removed line "-- old flag" renders as "--- old flag", and an added line
    # "++ new flag" as "+++ new flag". Before the hunk guard these were taken as
    # file headers: the path became "new flag" and both lines left the index.
    first = ai.parse_diff(DIFF)[0]
    assert first["path"] == "x.txt"
    assert [line[1] for line in first["lines"]] == [
        "@@ -1,3 +1,3 @@", " keep", "--- old flag", "+++ new flag", " keep2"]


# ---------------------------------------------------------------- model answer

def test_extract_json_skips_prose_and_objects_without_findings():
    raw = 'Here you go:\n{"note": "x"}\n{"findings": [{"file": "a"}]}'
    assert ai.extract_json(raw)["findings"] == [{"file": "a"}]


def test_extract_json_strips_a_code_fence():
    assert ai.extract_json('```json\n{"findings": []}\n```') == {"findings": []}


# ---------------------------------------------------------------- filtering

def test_verify_attaches_location_and_drops_an_unquoted_finding():
    parsed = ai.parse_diff(
        "diff --git a/engine.cpp b/engine.cpp\n"
        "--- a/engine.cpp\n"
        "+++ b/engine.cpp\n"
        "@@ -10,1 +10,1 @@\n"
        "-    return 1;\n"
        "+    return 2;\n")
    findings = [
        {"file": "engine.cpp", "evidence": "+    return 2;", "why": "result changed"},
        {"file": "engine.cpp", "evidence": "+    return 3;", "why": "invented"},
    ]
    kept, dropped = ai.verify(findings, parsed, [], [])
    assert [(it["_path"], it["_line"]) for it in kept] == [("engine.cpp", 10)]
    assert dropped[0]["_drop"] == "evidence not in diff"


def test_verify_drops_a_finding_that_refutes_itself():
    # The model sometimes writes the refutation into `why` ("This is
    # consistent; not a finding."). Publishing that tells the author nothing.
    parsed = ai.parse_diff(
        "diff --git a/engine.cpp b/engine.cpp\n"
        "--- a/engine.cpp\n"
        "+++ b/engine.cpp\n"
        "@@ -10,1 +10,1 @@\n"
        "-    return 1;\n"
        "+    return 2;\n")
    findings = [
        {"file": "engine.cpp", "evidence": "+    return 2;",
         "why": "This is consistent; not a finding."},
        # The phrasing the model actually produced on PR #64: it opens by
        # saying there is nothing to report, then explains why.
        {"file": "engine.cpp", "evidence": "+    return 2;",
         "why": "No finding: the fallback opens with the heading and the marker "
                "is prepended, so the opening check matches."},
        {"file": "engine.cpp", "evidence": "+    return 2;",
         "why": "The guard is present and tested; the tool also deletes the "
                "output at startup. No regression."},
        # A contrast marker after the refutation carries a live claim, so this
        # one is kept: the model refuted one part, not the finding.
        {"file": "engine.cpp", "evidence": "+    return 2;",
         "why": "The default is consistent, but the loader still reads the old key."},
    ]
    kept, dropped = ai.verify(findings, parsed, [], [])
    assert [it["why"] for it in kept] == [
        "The default is consistent, but the loader still reads the old key."]
    assert [it["_drop"] for it in dropped] == ["self-refuted"] * 3


def test_locate_prefers_a_changed_line_over_an_identical_context_line():
    # A markerless quote that matches both an unchanged context line and an
    # added line must resolve to the added one; otherwise the finding is pinned
    # to a line this PR did not touch.
    parsed = ai.parse_diff(
        "diff --git a/a.cpp b/a.cpp\n"
        "--- a/a.cpp\n"
        "+++ b/a.cpp\n"
        "@@ -1,2 +1,3 @@\n"
        " same();\n"
        " same();\n"
        "+same();\n")
    index = [(text.rstrip(), text.rstrip().lstrip("+- ").strip(), kind, old_ln, new_ln, f["path"])
             for f in parsed for kind, text, old_ln, new_ln in f["lines"]]
    hit = ai._locate("same();", index)
    assert hit[2] == "A"
    assert hit[4] == 3


def test_locate_does_not_match_a_block_that_spans_two_files():
    # The index is one flat list across files, so the last line of one file is
    # adjacent to the first line of the next. A two-line quote that only exists
    # by stitching those neighbours together must not resolve.
    index = [("return;", "", "A", 1, 1, "a.cpp"),
             ("return;", "", "A", 1, 1, "b.cpp")]
    assert ai._locate("return;\nreturn;", index) is None


def test_collapse_merges_the_same_location_and_quote():
    kept = [
        {"_path": "a", "_line": 1, "evidence": "+x"},
        {"_path": "a", "_line": 1, "evidence": "+x"},
        {"_path": "a", "_line": 1, "evidence": "+y"},
    ]
    out = ai.collapse(kept)
    assert len(out) == 2
    assert out[0]["_repeat"] == 2


def test_invariants_for_uses_the_longest_prefix():
    assert ai.invariants_for("CellEvoX/src/systems/engine.cpp") == [1, 3, 4, 5, 8]
    assert ai.invariants_for("CellEvoX/scripts/snapshot_io.py") == [6, 7]
    assert ai.invariants_for("docs/readme.md") == []


def test_invariant_matches_number_title_and_negative_space():
    allowed = ["1. **Deterministic replay** - same seed, same output.",
               "Anything that changes the snapshot ABI needs an ADR."]
    assert ai.invariant_matches("1", allowed)
    assert ai.invariant_matches("deterministic replay", allowed)
    assert ai.invariant_matches("Negative Space: snapshot ABI", allowed)
    assert not ai.invariant_matches("7", allowed)


# ---------------------------------------------------------------- context

def test_build_context_always_sends_invariants_inventory_and_file_body(tmp_path):
    repo = _make_repo(tmp_path)
    payload, meta = ai.build_context(
        repo, "HEAD~1...HEAD", "HEAD", "change the step result", 1500, 1_000_000)

    assert "TEST INVENTORY (Catch2 in CellEvoX/tests" in payload
    assert "deterministic replay" in payload
    assert "Deterministic replay" in payload
    assert "Anything that changes the snapshot ABI" in payload
    assert "--- FULL FILE CellEvoX/src/systems/engine.cpp" in payload
    assert meta["files"] == 1
    assert meta["inventory"] == 2


def test_test_inventory_includes_python_tests(tmp_path):
    # A pytest test asserts behaviour like a Catch2 case; before this, a real
    # pytest name in `coverage` was flagged INVENTED_TEST on a workflow-only PR.
    repo = _make_repo(tmp_path)
    widget = repo / "tools/test_widget.py"
    widget.parent.mkdir(parents=True)
    widget.write_text("def test_widget_works():\n    assert True\n")
    io_test = repo / "CellEvoX/scripts/test_io.py"
    io_test.parent.mkdir(parents=True)
    io_test.write_text("class TestReader:\n    def test_reader_fields(self):\n        pass\n")

    inventory = ai.test_inventory(repo)
    assert "deterministic replay [systems]" in inventory
    assert "test_widget_works (test_widget.py)" in inventory
    assert "test_reader_fields (test_io.py)" in inventory
    assert len(inventory) == len(set(inventory))


def test_build_context_gives_infrastructure_files_their_own_rules(tmp_path):
    # .github/, tools/ and docs/ carry no numbered invariant. The bare "none
    # matched" line left the model without any rule for secrets, pins and
    # fail-open checks, so those files get an explicit scope instead.
    repo = _make_repo(tmp_path)
    workflow = repo / ".github/workflows/ci.yml"
    workflow.parent.mkdir(parents=True)
    workflow.write_text("on: push\njobs: {}\n")
    _git(repo, "add", "-A")
    _git(repo, "commit", "-q", "-m", "add ci")

    payload, meta = ai.build_context(
        repo, "HEAD~1...HEAD", "HEAD", "add ci", 0, 1_000_000)

    assert "INFRASTRUCTURE REVIEW SCOPE" in payload
    assert "none matched the changed paths" not in payload
    assert meta["infra"] is True
    allowed = ai.allowed_invariants(
        repo, meta["invariants"], meta["negative_space"], meta["infra"])
    # The guidance is part of the payload, so citing it is not an invented
    # invariant.
    assert ai.invariant_matches("infrastructure review scope", allowed)


def test_a_zero_line_cap_still_sends_file_bodies(tmp_path):
    # 0 is "no line cap": --max-chars already bounds the payload, so a separate
    # line ceiling only costs the context needed to judge the change.
    repo = _make_repo(tmp_path)
    payload, meta = ai.build_context(
        repo, "HEAD~1...HEAD", "HEAD", "change the step result", 0, 1_000_000)

    assert meta["diff_only"] is False
    assert "--- FULL FILE CellEvoX/src/systems/engine.cpp" in payload
    assert not meta["dropped"]


def test_build_context_marks_a_diff_only_review(tmp_path):
    repo = _make_repo(tmp_path)
    payload, meta = ai.build_context(
        repo, "HEAD~1...HEAD", "HEAD", "change the step result", 1, 1_000_000)

    assert meta["diff_only"] is True
    assert "--- FULL FILE" not in payload
    assert meta["dropped"] == ["above the 1-line threshold (2 changed lines)"]


def test_build_context_is_diff_only_when_the_only_body_is_too_large(tmp_path):
    # The line cap is not the only way to lose every body: a file above
    # MAX_BODY_CHARS is skipped for size, and that is the same blind spot.
    repo = _make_repo(tmp_path)
    big = repo / "CellEvoX/src/systems/big.cpp"
    pad = "// " + "x" * ai.MAX_BODY_CHARS + "\n"
    big.write_text(pad + "int big() {\n    return 1;\n}\n")
    _git(repo, "add", "-A")
    _git(repo, "commit", "-q", "-m", "add big")
    big.write_text(pad + "int big() {\n    return 2;\n}\n")
    _git(repo, "commit", "-q", "-am", "change big")

    payload, meta = ai.build_context(
        repo, "HEAD~1...HEAD", "HEAD", "change big", 0, 1_000_000)

    assert meta["diff_only"] is True
    assert "--- FULL FILE" not in payload
    assert any("chars >" in d for d in meta["dropped"])


def test_render_markdown_publishes_a_diff_only_blocker_as_a_warning():
    meta = {"files": 1, "changed_lines": 2, "invariants": [], "payload_chars": 10,
            "est_input_tokens": 3, "dropped": ["engine.cpp: 90000 chars > 24000"],
            "diff_only": True}
    finding = {"_path": "CellEvoX/src/systems/engine.cpp", "_line": 2,
               "severity": "blocker", "behavior": "step() returns 2",
               "why": "callers assume 1"}
    md = ai.render_markdown(meta, [finding], [], "deepseek/test")

    assert "1 warning" in md
    assert "WARNING — `CellEvoX/src/systems/engine.cpp:2`" in md
    assert "DOWNGRADED_DIFF_ONLY" in md
    assert "diff-only" in md


def test_build_context_refuses_a_payload_over_the_budget(tmp_path):
    # The diff itself can exceed the ceiling; the tool must stop instead of
    # sending a truncated payload or silently dropping the diff.
    repo = _make_repo(tmp_path)
    # The message names the cause: every body was already dropped, so the diff
    # is what does not fit and --max-changed-lines would not change it.
    with pytest.raises(SystemExit, match="diff alone exceeds the budget"):
        ai.build_context(repo, "HEAD~1...HEAD", "HEAD", "change the step result",
                         1500, 100)


def test_build_context_drops_the_largest_body_first(tmp_path):
    # One large file must not starve the rest: the budget drops bodies from the
    # largest down and names each drop in `trimmed`, so the smaller file still
    # travels with the diff.
    repo = _make_repo(tmp_path)
    big = repo / "CellEvoX/src/systems/big.cpp"
    big.write_text("// pad\n" * 1000)
    small = repo / "CellEvoX/src/systems/small.cpp"
    small.write_text("// small\n" * 10)
    _git(repo, "add", "-A")
    _git(repo, "commit", "-q", "-m", "add two bodies")

    full, _ = ai.build_context(
        repo, "HEAD~2...HEAD", "HEAD", "add bodies", 1500, 10_000_000)
    # Between the full payload and the payload without the large body.
    budget = len(full) - 500
    payload, meta = ai.build_context(
        repo, "HEAD~2...HEAD", "HEAD", "add bodies", 1500, budget)

    assert meta["trimmed"] == [
        f"CellEvoX/src/systems/big.cpp: dropped to fit {budget} chars"]
    assert "--- FULL FILE CellEvoX/src/systems/small.cpp" in payload
    assert "--- FULL FILE CellEvoX/src/systems/big.cpp" not in payload


def test_assert_head_matches_range_rejects_a_different_commit(tmp_path):
    repo = _make_repo(tmp_path)
    with pytest.raises(SystemExit):
        ai.assert_head_matches_range(repo, "HEAD~1...HEAD", "HEAD~1")


# ---------------------------------------------------------------- comment

def test_render_markdown_names_the_model_and_no_prompt_flag():
    meta = {"files": 1, "changed_lines": 2, "invariants": [],
            "payload_chars": 10, "est_input_tokens": 2, "dropped": []}
    md = ai.render_markdown(meta, [], [], "deepseek/test")
    assert md.splitlines()[0] == "### AI diff review — `deepseek/test`"
    assert "No findings" in md


def test_render_markdown_neutralizes_mentions_and_keeps_the_fence_closed():
    # The evidence is diff text the model picked, and the PR author controls the
    # diff: a quote holding ``` would close the block, and a quote holding
    # @name would ping someone from an advisory comment.
    meta = {"files": 1, "changed_lines": 2, "invariants": [], "payload_chars": 10,
            "est_input_tokens": 2, "dropped": []}
    kept = [{"severity": "warning", "_path": "a.cpp", "_line": 1,
             "evidence": "+see ```\n+@team", "why": "pings @team"}]
    md = ai.render_markdown(meta, kept, [], "deepseek/test")
    assert "````diff" in md  # one more backtick than the quote holds
    assert "@\u200bteam" in md
    assert "@team" not in md


def test_every_mapped_invariant_exists_in_agents_md():
    # INVARIANTS_FOR is a second copy of the contract's numbering: renumbering
    # AGENTS.md must fail here instead of silently injecting the wrong sections
    # for a changed path.
    sections = ai.load_invariant_sections(TOOL.parent.parent)
    mapped = {n for ids in ai.INVARIANTS_FOR.values() for n in ids}
    assert mapped == set(sections), sorted(mapped ^ set(sections))


def test_invariant_mapping_for_representative_paths():
    # The set check above catches renumbering, not a mapping moved to the wrong
    # path. These paths are the reason each mapping exists, so a change here
    # changes what the prompt injects and has to be deliberate.
    assert ai.invariants_for("CellEvoX/src/systems/Simulation.cpp") == [1, 3, 4, 5, 8]
    assert ai.invariants_for("CellEvoX/include/utils/DeterministicRng.hpp") == [2]
    assert ai.invariants_for("CellEvoX/scripts/snapshot_io.py") == [6, 7]
    assert ai.invariants_for("CellEvoX/scripts/run.py") == [7]
    assert ai.invariants_for("docs/adr/0010-advisory-ai-diff-review.md") == []


def test_robin_reviews_with_the_same_default_model():
    # The model is written in three places (the tool's default, ai-review.yml
    # and robin.yml); two arms on different models would make their comments
    # incomparable for reasons that have nothing to do with the reviewer.
    workflow = (TOOL.parent.parent / ".github/workflows/robin.yml").read_text()
    assert re.search(r"^\s{10}model: (\S+)$", workflow, re.M).group(1) == ai.DEFAULT_MODEL
    ci = (TOOL.parent.parent / ".github/workflows/ai-review.yml").read_text()
    assert re.search(r"--model (\S+) \\", ci).group(1) == ai.DEFAULT_MODEL


def test_a_markerless_quote_does_not_match_text_that_starts_with_a_marker():
    # A context line whose code starts with "+" must not absorb a markerless
    # quote: stripping every leading marker turned " +keep" into "keep".
    index = [(" +keep", "+keep", "C", 1, 1, "a.py")]
    assert ai._locate("keep", index) is None


def test_published_findings_adds_a_majority_finding_the_published_run_missed():
    # The published run is one sample, so a finding most calls reported can be
    # absent from it. It is added back at its own support; a 1/N outlier is not,
    # because it never reaches the threshold.
    def finding(line, quote):
        return {"_path": "a.cpp", "_line": line, "evidence": quote}

    majority = finding(1, "+b")
    solo = finding(2, "+solo")
    results = [
        {"kept": [dict(majority), dict(solo)]},
        {"kept": [dict(majority), finding(3, "+c")]},
        {"kept": [finding(4, "+a"), finding(5, "+x"), finding(6, "+y")]},
        {"kept": [dict(majority), finding(7, "+p"), finding(8, "+q"),
                  finding(9, "+r")]},
        {"kept": [finding(10, "+u"), finding(11, "+v"), finding(12, "+w"),
                  finding(13, "+z"), finding(14, "+t")]},
    ]
    kept = ai.published_findings(results, ai.median_run(results))
    lines = {(it["_path"], it["_line"]) for it in kept}
    assert ("a.cpp", 1) in lines      # 3/5, added although the chosen run missed it
    assert ("a.cpp", 2) not in lines  # 1/5 outlier stays out of the comment
    assert len(kept) == 4


def test_published_findings_needs_strict_majority_on_an_even_run_count():
    # "Majority" is strict, so exactly half of an even run count is not enough: a
    # finding two of four calls reported is a coin flip and stays out even though
    # the chosen run missed it. Three of four is a majority and is added back.
    def f(line):
        return {"_path": "a.cpp", "_line": line, "evidence": f"+{line}"}

    half = f(1)
    # Finding counts 0, 1, 3, 4 put the upper middle on the run without `half`.
    results = [
        {"kept": [dict(half)]},
        {"kept": []},
        {"kept": [f(3), f(4), f(5)]},
        {"kept": [dict(half), f(6), f(7), f(8)]},
    ]
    lines = {(it["_path"], it["_line"])
             for it in ai.published_findings(results, ai.median_run(results))}
    assert ("a.cpp", 1) not in lines

    most = f(1)
    # Finding counts 1, 1, 2, 4 put the upper middle on the run without `most`.
    results = [
        {"kept": [dict(most)]},
        {"kept": [dict(most)]},
        {"kept": [f(2), f(3)]},
        {"kept": [dict(most), f(4), f(5), f(6)]},
    ]
    lines = {(it["_path"], it["_line"])
             for it in ai.published_findings(results, ai.median_run(results))}
    assert ("a.cpp", 1) in lines


def test_render_markdown_labels_skipped_and_trimmed_bodies_separately():
    # An ineligible file and a body dropped to fit the budget are different
    # reasons; one label for both leaves the author unable to tell which.
    meta = {"files": 1, "changed_lines": 2, "invariants": [],
            "payload_chars": 10, "est_input_tokens": 2,
            "dropped": ["a.py: extension not in ..."],
            "trimmed": ["b.cpp: dropped to fit 1000 chars"]}
    md = ai.render_markdown(meta, [], [], "deepseek/test")
    assert "file bodies skipped: a.py" in md
    assert "dropped to fit the budget: b.cpp" in md


def test_render_markdown_names_failed_repeat_calls():
    # A repeat where only some calls returned still publishes a comment; the
    # header has to say so, or the support counts look like they cover every run.
    meta = {"files": 1, "changed_lines": 2, "invariants": [],
            "payload_chars": 10, "est_input_tokens": 2, "dropped": []}
    md = ai.render_markdown(meta, [], [], "deepseek/test", failed_runs=1)
    assert "1 repeat call failed" in md


def test_exactly_one_prompt_is_shipped():
    source = TOOL.read_text()
    assert source.count('\nSYSTEM = """') == 1
    assert "--prompt" not in source


def test_cli_writes_the_pr_comment_from_a_saved_response(tmp_path):
    repo = _make_repo(tmp_path)
    response = tmp_path / "response.json"
    response.write_text(json.dumps({"findings": [{
        "file": "CellEvoX/src/systems/engine.cpp",
        "severity": "blocker",
        "behavior": "step result",
        "was": "return 1;",
        "now": "return 2;",
        "coverage": "none",
        "evidence": "+    return 2;",
        "why": "The step result changed without a test.",
    }]}))
    out = tmp_path / "out"

    proc = subprocess.run([
        sys.executable, str(TOOL),
        "--repo", str(repo),
        "--range", "HEAD~1...HEAD",
        "--head", "HEAD",
        "--intent", "change the step result",
        "--response-file", str(response),
        "--repeat", "2",
        "--out", str(out),
        "--md-out", str(out / "comment.md"),
    ], capture_output=True, text=True)

    assert proc.returncode == 0, proc.stderr
    comment = (out / "comment.md").read_text()
    assert "### AI diff review" in comment
    assert "CellEvoX/src/systems/engine.cpp:2" in comment
    # Both runs produced the same finding, so the comment says so: without the
    # support count a 1/N outlier would read exactly like a stable finding.
    assert "support=2/2" in comment
    # --response-file replays one answer for every run, so two runs cannot
    # disagree here. The majority add-back path is covered end to end only at
    # the function level, by the published_findings test.


def test_unreadable_response_file_exits_with_one_reason(tmp_path):
    # The workflow quotes the tool's last stderr line, so an unreadable
    # --response-file has to leave a reason there, not a traceback.
    repo = _make_repo(tmp_path)
    proc = subprocess.run([
        sys.executable, str(TOOL),
        "--repo", str(repo),
        "--range", "HEAD~1...HEAD",
        "--head", "HEAD",
        "--intent", "no response file",
        "--response-file", str(tmp_path / "missing.json"),
        "--out", str(tmp_path / "out"),
    ], capture_output=True, text=True)

    assert proc.returncode != 0
    assert "cannot read --response-file" in proc.stderr
    assert "Traceback" not in proc.stderr


def test_median_run_ignores_a_failed_final_call():
    # A failed call has no findings; publishing it would claim "No findings".
    results = [{"kept": [1]}, {"kept": [1, 2, 3]}, {"error": "boom", "kept": []}]
    assert ai.median_run(results) is results[1]


def test_median_run_takes_the_upper_middle_of_a_tie():
    # Ties are settled by position, not by distance to the median: with counts
    # 1, 2, 2 the comment is the first of the two-finding runs.
    one, two_a, two_b = {"kept": [1]}, {"kept": [2, 3]}, {"kept": [4, 5]}
    assert ai.median_run([one, two_a, two_b]) is two_a
    assert ai.median_run([{"error": "x", "kept": []}]) is None


def test_median_run_takes_the_upper_middle_of_an_even_count():
    # Four successful runs with counts 0, 1, 1, 2: the upper middle is the
    # second one-finding run, so the result is a run that kept findings.
    results = [{"kept": []}, {"kept": [1]}, {"kept": [2]}, {"kept": [3, 4]}]
    assert ai.median_run(results) is results[2]


def test_support_counts_says_how_many_runs_produced_a_finding():
    # Identity is location plus quote, so the same finding in every run is one
    # entry at N/N and a one-run finding is 1/N, not a separate section.
    same = {"_path": "a.cpp", "_line": 3, "evidence": "+x"}
    only_once = {"_path": "a.cpp", "_line": 9, "evidence": "+y"}
    results = [{"kept": [dict(same), dict(only_once)]},
               {"kept": [dict(same)]},
               {"kept": [dict(same)]}]
    supported = ai.support_counts(results)
    assert [(it["_path"], it["_line"], it["_support"]) for it in supported] == [
        ("a.cpp", 3, "3/3"), ("a.cpp", 9, "1/3")]


def test_support_counts_excludes_failed_calls_from_the_denominator():
    # A failed call has no findings at all, so it is not evidence against one: a
    # finding every successful call reported must read 2/2, not 2/3. The comment
    # header names the failed call separately.
    same = {"_path": "a.cpp", "_line": 3, "evidence": "+x"}
    results = [{"kept": [dict(same)]},
               {"kept": [], "error": "unparseable response"},
               {"kept": [dict(same)]}]
    supported = ai.support_counts(results)
    assert [(it["_path"], it["_support"]) for it in supported] == [("a.cpp", "2/2")]


def test_google_key_is_sent_in_a_header_not_the_url(monkeypatch):
    # A key in the query string is copied into every log line that touches the
    # URL, including the provider's own error text. The header keeps it out.
    seen = {}

    class Response:
        def __enter__(self):
            return self

        def __exit__(self, *exc):
            return False

        def read(self, *args):
            return json.dumps({"candidates": [{"content": {"parts": [{"text": "{}"}]}}]}).encode()

    def fake_urlopen(req, timeout):
        seen["url"] = req.full_url
        seen["headers"] = {k.lower(): v for k, v in req.header_items()}
        return Response()

    monkeypatch.setenv("GEMINI_API_KEY", "secret-key")
    monkeypatch.setattr(ai.urllib.request, "urlopen", fake_urlopen)
    ai.call_google("gemini-test", "system", "user", 0.0, 100)
    assert "secret-key" not in seen["url"]
    assert seen["headers"]["x-goog-api-key"] == "secret-key"


def test_a_transient_network_error_is_retried_then_reported(monkeypatch):
    # A reset connection or a read timeout is what the retry loop exists for.
    # It must be retried, and the last attempt must exit with a reason the
    # workflow can quote rather than a traceback.
    attempts = []

    def fake_urlopen(req, timeout):
        attempts.append(1)
        raise ai.urllib.error.URLError("connection reset")

    monkeypatch.setenv("OPENROUTER_API_KEY", "k")
    monkeypatch.setattr(ai.urllib.request, "urlopen", fake_urlopen)
    monkeypatch.setattr(ai.time, "sleep", lambda seconds: None)
    with pytest.raises(SystemExit) as exc:
        ai.call_openrouter("model", "system", "user", 0.0, 100, "off")
    assert len(attempts) == 6
    assert "request failed" in str(exc.value)


class _StubResponse:
    def __init__(self, payload):
        self._payload = payload

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def read(self, *args):
        return json.dumps(self._payload).encode()


def test_a_malformed_reply_is_a_reason_not_a_traceback(monkeypatch):
    # An unexpected shape (no choices, an empty list, a non-string content) must
    # leave a reason on stderr: the workflow quotes that line, and a traceback
    # would be quoted instead.
    monkeypatch.setenv("OPENROUTER_API_KEY", "k")
    for payload, expected in (
            ({}, "no choices in response"),
            ({"choices": []}, "no choices in response"),
            ({"choices": [{"message": {"content": ["parts"]}}]},
             "unexpected content type")):
        monkeypatch.setattr(ai.urllib.request, "urlopen",
                            lambda req, timeout, p=payload: _StubResponse(p))
        with pytest.raises(SystemExit) as exc:
            ai.call_openrouter("model", "system", "user", 0.0, 100, "off")
        assert expected in str(exc.value)


def test_an_empty_google_reply_names_its_finish_reason(monkeypatch):
    monkeypatch.setenv("GEMINI_API_KEY", "k")
    monkeypatch.setattr(
        ai.urllib.request, "urlopen",
        lambda req, timeout: _StubResponse(
            {"candidates": [{"finishReason": "SAFETY", "content": {"parts": []}}]}))
    with pytest.raises(SystemExit) as exc:
        ai.call_google("gemini-test", "system", "user", 0.0, 100)
    assert "empty content" in str(exc.value)
    assert "SAFETY" in str(exc.value)


def test_repeat_with_every_call_failed_writes_no_comment(tmp_path):
    # --md-out must not turn an all-failed run into "No findings": nothing is
    # written and the exit code is the tool's "no result" code, so the workflow
    # posts its fallback instead of a stale or empty comment.
    repo = _make_repo(tmp_path)
    response = tmp_path / "response.txt"
    response.write_text("not json at all")
    out = tmp_path / "out"

    proc = subprocess.run([
        sys.executable, str(TOOL),
        "--repo", str(repo),
        "--range", "HEAD~1...HEAD",
        "--head", "HEAD",
        "--intent", "change the step result",
        "--response-file", str(response),
        "--repeat", "2",
        "--out", str(out),
        "--md-out", str(out / "comment.md"),
    ], capture_output=True, text=True)

    assert proc.returncode == 4, proc.stderr
    assert not (out / "comment.md").exists()


def test_a_single_unparseable_run_removes_a_stale_comment(tmp_path):
    # The same "no result" contract for --repeat 1: an unreadable answer exits
    # with the no-result code, and a comment.md left by an earlier invocation
    # must not survive it, or a consumer would publish the old review.
    repo = _make_repo(tmp_path)
    response = tmp_path / "response.txt"
    response.write_text("not json at all")
    out = tmp_path / "out"
    out.mkdir()
    comment = out / "comment.md"
    comment.write_text("### AI diff review\n\nstale\n")

    proc = subprocess.run([
        sys.executable, str(TOOL),
        "--repo", str(repo),
        "--range", "HEAD~1...HEAD",
        "--head", "HEAD",
        "--intent", "change the step result",
        "--response-file", str(response),
        "--out", str(out),
        "--md-out", str(comment),
    ], capture_output=True, text=True)

    assert proc.returncode == 4, proc.stderr
    assert not comment.exists()


def test_robin_workflow_is_pinned_and_advisory():
    import re

    workflow = (TOOL.parent.parent / ".github/workflows/robin.yml").read_text()
    # A branch ref would let an upstream change alter what runs in this repo.
    assert re.search(r"antongulin/robin@[0-9a-f]{40}\b", workflow)
    # Robin runs next to the review, so it must never block a merge.
    assert 'request-changes: "false"' in workflow
    # Agent mode would send files outside the diff; the egress is bounded to it.
    assert 'agent-mode: "off"' in workflow
    # "none" is OpenRouter's explicit disable; "off" sends nothing and leaves the
    # model's default on, which spent the completion budget on reasoning.
    assert 'reasoning-effort: "none"' in workflow
    # Single-shot mode cannot read a file on its own, so naming AGENTS.md in the
    # inline text is not enough: the file input is what puts the contract in the
    # prompt, for the comparison to be against the same contract.
    assert "review-instructions-file: AGENTS.md" in workflow
    # The diff ceiling is kept equal to the tool's payload ceiling. They measure
    # different things, so equality is not a guarantee, but it means an edit to
    # one default shows up here instead of silently making one arm refuse first.
    assert 'max-diff-size: "1000000"' in workflow
    assert "default=1000000" in TOOL.read_text()


def test_robin_workflow_passes_the_actions_declared_inputs():
    # The inputs are kebab-case (llm-api-key, llm-base-url, model), not the
    # LLM_* environment variables. An unknown key is ignored by the action,
    # which then falls back to its default, so every key passed here has to be
    # one the action declares.
    import re

    workflow = (TOOL.parent.parent / ".github/workflows/robin.yml").read_text()
    declared = {
        "github-token", "llm-api-key", "llm-base-url", "model", "fail-on-high",
        "request-changes", "max-diff-size", "estimated-request-token-budget",
        "max-output-tokens", "reasoning-effort", "llm-timeout-ms", "llm-temperature",
        "max-comments", "review-on-synchronize", "min-command-permission",
        "bot-review-policy", "bot-allowlist", "review-instructions",
        "review-instructions-file", "config-file", "agent-mode", "agent-max-turns",
        "agent-max-diff-size", "use-json-response-mode",
    }
    passed = set(re.findall(r"^\s{10}([a-z][a-z0-9-]*):", workflow, re.M))
    assert {"llm-api-key", "llm-base-url", "model"} <= passed
    assert passed <= declared, sorted(passed - declared)


def test_review_workflow_surfaces_why_no_result_was_produced():
    workflow = (TOOL.parent.parent / ".github/workflows/ai-review.yml").read_text()
    # The tool's stderr is kept and quoted in the fallback comment, so an
    # over-budget payload reads differently from a provider failure.
    assert "2>review-out/reason.txt" in workflow
    assert "REVIEW_REASON" in workflow
    # The step must fail before the job timeout: a job-level timeout cancels the
    # run, and a cancelled run skips the step that posts the fallback.
    assert "timeout-minutes: 12" in workflow
    # `always()` is true on cancellation as well as on failure, and the cancelled
    # runs of this workflow have executed the post step. `!cancelled()` keeps the
    # failure path and drops the cancellation path; cancellation alone does not
    # cover a run that finished after a newer push, so the script also compares
    # the event's head with the PR head before it writes.
    post_step = workflow.split("Post or update the review comment")[1].split("- name:")[0]
    assert "if: ${{ !cancelled() }}" in post_step
    assert "if: always()" not in post_step
    assert "pr.head.sha !== head" in post_step
    # A superseded run must not turn the PR red. Cancellation is not the only
    # way to be superseded: a run that finished after a newer push, or was
    # re-run on an old commit, is not cancelled either. The post step publishes
    # that second kind, so the failure step excludes both.
    assert "id: post" in post_step
    assert "core.setOutput('superseded', 'true')" in post_step
    fail_step = workflow.split("Fail when the review could not run")[1]
    assert ("if: ${{ !cancelled() && steps.post.outputs.superseded != 'true' "
            "&& steps.review.outcome != 'success' }}") in fail_step
    # An empty stderr leaves the fallback with nothing to quote, which must read
    # as "no reason was written", not as a silent omission.
    assert "No reason was written by the review step." in workflow
    # One budget for CI and local runs: passing --max-chars here would make a
    # local run without the flag refuse a payload that CI accepts.
    assert not re.findall(r"^\s+--max-chars", workflow, re.M)
    assert "default=1000000" in (TOOL).read_text()


def test_review_workflow_says_when_the_comment_was_truncated():
    workflow = (TOOL.parent.parent / ".github/workflows/ai-review.yml").read_text()
    post_step = workflow.split("Post or update the review comment")[1].split("- name:")[0]
    # Slicing the composed body dropped the tail of the findings and the
    # advisory line without saying so. The report is shortened before the
    # advisory is appended, and the cut is named in the comment.
    assert ").slice(0, 60000);" not in post_step
    assert "Report truncated at the comment size limit" in post_step
    assert "const advisory = '_Advisory: findings do not block the merge._';" in post_step


def test_review_workflow_finds_the_sticky_comment_by_bot_not_login():
    workflow = (TOOL.parent.parent / ".github/workflows/ai-review.yml").read_text()
    assert "c.user?.type === 'Bot'" in workflow
    # The marker and the review's heading must open the body: a body that merely
    # quotes the marker, or opens with the marker alone, is not ours.
    assert "c.body?.startsWith(opening)" in workflow
    assert "const opening = `${marker}\\n### AI diff review`;" in workflow
    # The fallback posted on a failed run goes through the same upsert, so it has
    # to open with that heading too: a fallback without it would be posted with
    # the marker but never found again, and the next push would start a second
    # comment instead of rewriting this one.
    assert "const fallback = '### AI diff review\\n\\n' +" in workflow
    # Both the report and the fallback go through one composed body, and that
    # body is what the marker opens. A fallback is therefore found and rewritten
    # like any other review, instead of piling up one comment per failed run.
    assert "const body = `${marker}\\n${shown}\\n\\n${advisory}`;" in workflow
    assert "c.body?.includes(marker)" not in workflow
    assert "c.user?.login === 'github-actions[bot]'" not in workflow
