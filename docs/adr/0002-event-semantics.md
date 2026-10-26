# ADR-0002: Birth replaces the parent with two daughters
Status: Proposed · 2026-10-24

## Context
Lineage tracking requires the parent's identity and mutation history to survive
division. Keeping the parent plus two children would inflate population and break
lineage accounting.
## Decision
A birth removes the parent and installs exactly two daughters carrying its
fitness and mutations plus any new mutation. Daughter ordering uses an explicit
total tie-break chain. `Cell::parent_id` records the parent, while mutation
payload ids record the cell in which each mutation originated: inherited ids
survive unchanged, and a new mutation's placeholder is rewritten to the
daughter's own id after id assignment.
## Consequences
- Population moves only by +1 (birth) and -1 (death).
- Changing ordering, parent linkage, or mutation-origin rewriting changes
  results and snapshot contents.
- Mutation probability is bounded to `[0, 1]` with a small tolerance.
## Evidence
To be recorded before this ADR moves to `Accepted` (code pointer, test, and a
sample snapshot/CSV pair).
