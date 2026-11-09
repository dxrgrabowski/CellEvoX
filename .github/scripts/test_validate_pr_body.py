#!/usr/bin/env python3
"""Tests for the pull request body structure check."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

from validate_pr_body import build_report


def valid_body(sha: str) -> str:
    return f"""## What changed and why

The body check now reads structure instead of a YAML contract.

## Acceptance criteria

Missing sections and empty evidence are rejected.

## Verification

Verified commit: {sha}

`python3 .github/scripts/test_validate_pr_body.py` exited 0.

Not checked: whether the description is true.
"""


class BodyCheckTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = cls.tmp.name
        subprocess.run(["git", "init", "-q", cls.root], check=True)
        subprocess.run(["git", "-C", cls.root, "config", "user.email", "test@example.com"], check=True)
        subprocess.run(["git", "-C", cls.root, "config", "user.name", "Test"], check=True)
        Path(cls.root, "file.txt").write_text("x", encoding="utf-8")
        subprocess.run(["git", "-C", cls.root, "add", "file.txt"], check=True)
        subprocess.run(["git", "-C", cls.root, "commit", "-qm", "init"], check=True)
        cls.sha = subprocess.run(
            ["git", "-C", cls.root, "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def errors(self, body: str) -> list[str]:
        return build_report(body, self.root).errors

    def test_complete_body_passes(self) -> None:
        self.assertEqual(self.errors(valid_body(self.sha)), [])

    def test_na_criteria_passes(self) -> None:
        body = valid_body(self.sha).replace(
            "Missing sections and empty evidence are rejected.", "N/A — documentation only."
        )
        self.assertEqual(self.errors(body), [])

    def test_leftover_comment_fails(self) -> None:
        body = valid_body(self.sha).replace("## What changed and why", "<!-- fill this in -->\n\n## What changed and why")
        self.assertTrue(any("HTML comment" in error for error in self.errors(body)))

    def test_comment_inside_fence_is_output(self) -> None:
        body = valid_body(self.sha).replace(
            "Not checked: whether the description is true.",
            "```\n<!-- output, not scaffolding -->\n```\n\nNot checked: whether the description is true.",
        )
        self.assertEqual(self.errors(body), [])

    def test_missing_section_fails(self) -> None:
        body = valid_body(self.sha).replace("## Acceptance criteria", "## Notes")
        self.assertTrue(any("missing '## Acceptance criteria'" in error for error in self.errors(body)))

    def test_empty_section_fails(self) -> None:
        body = valid_body(self.sha).replace(
            "The body check now reads structure instead of a YAML contract.", ""
        )
        self.assertTrue(any("'## What changed and why' is empty" in error for error in self.errors(body)))

    def test_empty_code_block_is_not_evidence(self) -> None:
        body = valid_body(self.sha).replace(
            "`python3 .github/scripts/test_validate_pr_body.py` exited 0.", "```\n```"
        )
        self.assertTrue(any("empty code block" in error for error in self.errors(body)))

    def test_heading_inside_fence_is_not_structure(self) -> None:
        body = valid_body(self.sha).replace("## Verification", "```\n## Verification\n```")
        self.assertTrue(any("missing '## Verification'" in error for error in self.errors(body)))

    def test_empty_verified_commit_fails(self) -> None:
        body = valid_body(self.sha).replace(f"Verified commit: {self.sha}", "Verified commit:")
        self.assertTrue(any("'Verified commit:' is empty" in error for error in self.errors(body)))

    def test_short_sha_fails(self) -> None:
        body = valid_body(self.sha).replace(self.sha, self.sha[:12])
        self.assertTrue(any("full 40-character SHA" in error for error in self.errors(body)))

    def test_unknown_commit_fails(self) -> None:
        body = valid_body("0" * 40)
        self.assertTrue(any("names no commit" in error for error in self.errors(body)))

    def test_label_inside_fence_does_not_count(self) -> None:
        body = valid_body(self.sha).replace(f"Verified commit: {self.sha}", f"```\nVerified commit: {self.sha}\n```")
        self.assertTrue(any("'Verified commit:' must appear once" in error for error in self.errors(body)))

    def test_missing_not_checked_fails(self) -> None:
        body = valid_body(self.sha).replace("Not checked: whether the description is true.", "")
        self.assertTrue(any("'Not checked:' must appear once" in error for error in self.errors(body)))

    def test_repeated_label_fails(self) -> None:
        body = valid_body(self.sha) + f"\nVerified commit: {self.sha}\n"
        self.assertTrue(any("'Verified commit:' must appear once" in error for error in self.errors(body)))


if __name__ == "__main__":
    unittest.main()
