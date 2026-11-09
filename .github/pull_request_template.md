---
# Machine-read contract: the human and AI reviewer parse these fields and the
# `PR change contract` gate checks them against the diff. Required: intent, risk,
# scope.paths. Keep the values current — the gate reads them, not their history.
intent: ""                # 1 sentence: the outcome this PR delivers
touches_invariants: []    # AGENTS.md invariant slugs this diff could break; the reviewer checks every applicable one, listed or not
risk: ""                  # low | medium | high — the author's estimate, not the reviewer's minimum: every review reads the whole diff, affected consumers, and applicable invariants. Higher risk raises the evidence bar and may add owner sign-off
scope:
  paths: []               # repo-relative globs: the map of this diff. '*' stops at '/', '**' crosses it; renames count on both paths. Refining the map is an ordinary PR update
  must_not_change: []     # globs the diff must never touch; empty means none
---

<!-- The body mixes two kinds of line. Schema keys stay, exactly once each, with
     the answer on the same line: `Behavioral delta:`,
     `Approach / rejected alternatives:`, `Requirement source:`,
     `Docs / ADR impact:`, `Verified commit:`, `Not checked:`, and either the
     criteria table or the `N/A` line that replaces it. Everything else is
     scaffolding — these comments, the bullet prompts, the sample `### AC1` —
     replaced with the answer and deleted: a leftover comment or an empty key
     fails the gate. The rules behind these sections are in AGENTS.md. -->

## Change

<!-- Observable before → after: values, names, formats, exit codes. A
     non-behavioral PR says "no runtime change: <what does change>". -->

Behavioral delta:

<!-- What you rejected, and the constraint that ruled it out — a build limit, a
     lifetime, an invariant — in a sentence or two. Write `none` when the diff
     takes the obvious route. This is what stops a reviewer proposing the
     alternative you already tried and proved broken. -->

Approach / rejected alternatives:

## Acceptance Criteria

<!-- One row per observable pass/fail condition; one may suffice. Phrase it so a
     reviewer can repeat the check. When the diff touches shared code, outputs, or
     formats, include a row for behavior that must stay unchanged.

     No behavioral criterion applies — a docs or comment change, not CI that
     changes builds, checks, or deployment: delete the table and write the answer
     as one line, `N/A — <reason>`. A row `| N/A | … |` is not an answer.

     Amendment after the PR opened: changing the intended behavior, a criterion,
     or an agreed constraint needs `Amended YYYY-MM-DD: <field> changed from
     <previous> to <new> because <reason>` under this section. Updating the file
     map, the implementation description, the risk estimate, or the evidence is an
     ordinary PR update. -->

<!-- Who asked, and for what: `owner request: <what was asked>`,
     `review: <link or round>`, `derived from the request: <the implication they
     drew>`, or `none — self-initiated: <what surfaced it>`. A change nothing
     external asked for says so; it does not invent a request. -->

Requirement source:

| ID | Observable condition |
|---|---|
| AC1 |  |

## Risk & Impact

<!-- `Docs / ADR impact:` is a schema key: keep it and answer it, starting with
     `none` only when no document changed, or with `updated:` / `override:` and
     naming the files or the ADR. The bullets below are prompts: keep the ones
     that apply and delete the rest. -->

Docs / ADR impact:

- **Risk justification:** required when risk is not low: what makes it so, and what catches a regression
- **Compatibility / migration / recovery:** only when a consumer, output, format, persisted artifact, or config changes
- **Out of scope / deferred:** adjacent problems this PR leaves alone, with a follow-up link; or none

## Verification

<!-- One block per criterion, headed by the criterion it verifies — "### AC1", or
     "### AC1, AC3" when one command settles several. Give the exact command, the
     exit status, and the decisive lines rather than the full log; long output
     belongs in a CI run or artifact linked here. A criterion with no block fails
     the gate, and a check you did not run is declared, never implied. Replacing
     this section on a later commit is an ordinary update, not an amendment.

     Criteria are `N/A`: delete the `### AC1` block and write the single line
     `N/A — no behavioral criteria` in its place. `Verified commit:` and
     `Not checked:` stay — a reviewer still needs to know what was verified
     against and what was not. -->

Verified commit:

### AC1

<!-- the command and its decisive lines -->

Not checked: <!-- checks you did not run, and why; or "Nothing" -->
