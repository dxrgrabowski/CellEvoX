---
# Change contract read by the human and AI reviewer. Required: intent, risk and
# scope.paths. An empty list means "none" for touches_invariants and must_not_change.
# Keep these values current: the gate checks them, not their history. A contract
# change updates the field and records the old value under "Amended" in the body.
intent: ""                # 1 sentence: why this change exists
touches_invariants: []    # Invariants (AGENTS.md) this diff could break — the reviewer verifies they still hold, and checks the ones omitted here. Changing one deliberately also needs owner sign-off and an Accepted ADR
risk: ""                  # low | medium | high — justify below when not low
scope:
  paths: []               # Repo-relative globs the diff touches — a map of the change, not a boundary it passed. '*' stops at '/', '**' crosses it; renames are checked on both old and new paths
  must_not_change: []     # Globs the diff must NOT touch; empty means none (Reviewer: this is the check — flag any touch here)
---

<!--
DELETE THIS COMMENT, AND EVERY OTHER HTML COMMENT IN THIS BODY, BEFORE OPENING THE PR.
An unresolved placeholder is an incomplete contract: the contract gate rejects
leftover HTML comments and template markers instead of reading them as intent.

1. `intent`, the acceptance criteria, and "Context & Behavioral Delta" define the
   target state. Treat explicitly declared behavioral changes as intentional;
   previous behavior remains the baseline for everything the contract does not
   change.
2. Intent describes the change; it does not bound the review. Read the complete
   diff and the callers, consumers, and formats it affects. Declared intent and
   scope do not waive correctness, compatibility, security, or any Hard Domain
   Invariant. Flag discrepancies between the implementation, the contract, and the
   accepted requirements — including an invariant broken without an ADR in
   `docs/adr/` with `Status: Accepted` in this diff.
3. One coherent outcome per PR. Implementation, tests, docs, and caller updates
   for that outcome belong together; unrelated work is a separate PR.
-->

## Context & Behavioral Delta

- **Why:** <!-- the constraint or failure that forced this choice; do not restate `intent` -->
- **Previous Behavior:** <!-- observable behavior before this PR -->
- **New Behavior:** <!-- observable behavior after; name specific mechanisms and values -->
- **Risk Justification:** <!-- required when `risk` is medium/high: what makes it so, what catches regressions (`N/A` if low) -->
- **Compatibility / Migration / Recovery:** <!-- `None`, or affected consumers outside this diff, required merge/deploy order, migration steps, and recovery limitations. Required for schema, persistent-state, API, and config changes -->
- **Docs & ADR Alignment:** <!-- tick exactly one; the contract gate rejects zero or multiple ticks -->
  - [ ] `No impact` — aligns with existing architecture and ADRs.
  - [ ] `Updated in this PR` — docs or ADRs corrected in this diff (list paths); plain documentation drift needs no ADR.
  - [ ] `Intentional Override` — deliberately deviates from `[doc/ADR path]` because `[reason]`. Breaking a Hard Domain Invariant additionally requires an ADR in `docs/adr/` with `Status: Accepted` in this diff.

## Acceptance Criteria

<!-- Requirement source: issue #N, ADR-XXXX, the owner request, a requirement
     discovered in session YYYY-MM-DD, or criteria derived from the request when
     they follow from it unambiguously. Ask the owner only about a real
     ambiguity, or when the answer chooses new domain semantics.

     - State observable behavior, not implementation steps. A criterion the
       reviewer cannot fail is not a criterion.
     - Use as many criteria as the observable behavior requires; one may
       suffice. When the diff touches shared code, outputs, or formats, include
       at least one criterion for behavior that must stay unchanged.
     - Bug fix: the failing case before and the passing case after. Perf: the
       named benchmark and its threshold.
     - If the list grows, check whether the PR contains separable outcomes.
     - `N/A — <reason>` only when no behavioral criterion applies. Plain docs
       and comment changes qualify; CI changes that affect builds, checks, or
       deployment do not.

     Keep the criteria and the front matter current: the gate and the reviewer
     read these values, not their history. An amendment updates the field and
     appends here `Amended YYYY-MM-DD: <field> changed from <previous> to <new>
     because <reason>`, and needs renewed review. Refreshing Verification
     Evidence alone is not an amendment. -->

Requirement source: <!-- issue/ADR/owner request, or "derived from the request: <sentence>" -->

| ID | Observable condition |
|---|---|
| AC1 | <!-- behavior after this PR --> |
| AC2 | <!-- behavior that must remain stable -->

## Scope Boundaries

- **In Scope:** <!-- the primary concern; plus adjacent fixes the Owner approved during the session, with the rationale. Nothing under `must_not_change`. -->
- **Deliberately Untouched / Known Gaps:** <!-- left alone or deferred, and why; link the follow-up issue/PR, or `None` -->

## Verification Evidence

Verified commit: <full SHA>

<!-- Each block is headed by the criterion it verifies — "### AC1", or
     "### AC1, AC3" when one command settles several — then the exact command as
     run and the decisive output: exit status, the pass/fail line, and any
     failure detail. Quote only what a reviewer can judge the criterion by. Full
     or noisy logs belong in an accessible CI run or uploaded artifact, linked
     here; artifacts expire, so the verdict and the lines it rests on must stay
     in the body. "Should work" and "all tests passed" without a command and
     quotable output are rejected. Every criterion needs a block; if a check was
     not run, state why. This section is the only one replaced on later commits
     without an amendment; it always names the commit it was produced against. -->

### AC1

<!-- command and its decisive output -->
