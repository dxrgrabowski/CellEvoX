---
# Machine-read contract: the human and AI reviewer parse these fields and the
# `PR change contract` gate checks them against the diff. Required: intent, risk,
# scope.paths. Keep the values current — the gate reads them, not their history.
intent: ""                # 1 sentence: the outcome this PR delivers
touches_invariants: []    # AGENTS.md invariant slugs this diff could break; the reviewer checks every applicable one, listed or not
risk: ""                  # low | medium | high — sets the review depth: low = delta + check; medium = also preserved-behavior criteria and consumers; high = also owner sign-off and invariant/ADR impact
scope:
  paths: []               # repo-relative globs: the map of this diff, and the budget for later commits of the same PR. '*' stops at '/', '**' crosses it; renames count on both paths
  must_not_change: []     # globs the diff must never touch; empty means none
---

<!-- Fill this in, then delete every HTML comment and template marker: leftovers
     fail the gate. The rules behind these sections are in AGENTS.md. -->

## Behavioral Delta

<!-- Observable before → after: values, names, formats, exit codes. A
     non-behavioral PR says "no runtime change: <what does change>". -->

Behavioral delta:

## Acceptance Criteria

<!-- One row per observable pass/fail condition; one may suffice. Phrase it so a
     reviewer can repeat the check. When the diff touches shared code, outputs, or
     formats, include a row for behavior that must stay unchanged.
     `N/A — <reason>` only when no behavioral criterion applies: a docs or comment
     change qualifies, CI that changes builds, checks, or deployment does not.

     Contract amendment after the PR opened: update the field, and add under this
     section `Amended YYYY-MM-DD: <field> changed from <previous> to <new> because
     <reason>`. Refreshing Verification alone is not an amendment. -->

Requirement source:

| ID | Observable condition |
|---|---|
| AC1 |  |

## Risk & Impact

<!-- Answer the lines that apply, delete the rest. `none` is an answer only when true. -->

Docs / ADR impact:

- **Risk justification:** required when risk is not low: what makes it so, and what catches a regression
- **Compatibility / migration / recovery:** only when a consumer, output, format, persisted artifact, or config changes
- **Out of scope / deferred:** adjacent problems this PR leaves alone, with a follow-up link; or none

## Verification

<!-- One block per criterion, headed by the criterion it verifies — "### AC1", or
     "### AC1, AC3" when one command settles several. Give the exact command, the
     exit status, and the decisive lines rather than the full log; long output
     belongs in a CI run or artifact linked here. A criterion with no block fails
     the gate, and a check you did not run is declared, never implied. This section
     is the only one that may be replaced on later commits without an amendment. -->

Verified commit:

### AC1

<!-- the command and its decisive lines -->

Not checked: <!-- checks you did not run, and why; or "Nothing" -->
