# ADR-0002: Birth replaces the parent with two daughters
Status: Accepted · 2026-10-24

## Context
Lineage tracking requires the parent's identity and mutation history to survive
division. Keeping the parent plus two children would inflate population and break
lineage accounting.
## Decision
A birth removes the parent and installs exactly two daughters carrying its
fitness and mutations plus any new mutation. Daughter ordering uses an explicit
total tie-break chain; mutation payloads reference the parent id, with a
placeholder rewritten after id assignment.
## Consequences
- Population moves only by +1 (birth) and -1 (death).
- Changing ordering or payload linkage changes results and snapshot contents.
- Mutation probability is bounded to `[0, 1]` with a small tolerance.
