---
# Machine-readable change contract. Validated by .github/workflows/quality-gates.yml.
# Leave a value empty and the check fails: the contract is the point of the PR.
intent: ""
touches_invariants: []
risk: ""
scope:
  paths: []
  must_not_change: []
verification: []
---

<!--
Contract rules (see AGENTS.md for the invariant names):
  risk               low | medium | high. Drives review depth.
  scope.paths        globs the diff MUST stay inside, e.g. "CellEvoX/src/**"
  scope.must_not_change  globs the diff MUST NOT touch, e.g. "CellEvoX/include/**"
  verification       commands a reviewer can re-run to check this PR
Example contract for a documentation-only change:

intent: "Replace drifted knowledge base with AGENTS.md and ADRs"
touches_invariants: [determinism, snapshot-abi]
risk: low
scope:
  paths: ["AGENTS.md", "docs/**", "README.md"]
  must_not_change: ["CellEvoX/**", "web/**"]
verification:
  - cmd: "git diff --name-only origin/main...HEAD | grep -v '\\.md$'"
    expect: "no output (docs-only diff)"
-->

## Why

What is wrong, missing, or costly today? Link the issue if there is one.

## What changed

Behaviour, not file list. Call out anything a reviewer would not infer from the diff.

## Verification

Paste the actual result of each command declared in `verification` above.
"Should work" is not verification.

## Known gaps

What you deliberately did not do, and why.
