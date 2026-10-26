# ADR-0003: Dense 2D storage and the duplicated event loop
Status: Proposed — debt acknowledged · 2026-10-24

## Context
The stochastic simulator came first on concurrent hash maps. The dense-array
optimization (`904f5dd`) later added a second, faster event loop for the 2D
stochastic path, leaving two copies of the same loop (dense 2D versus the shared
step used by 3D). The separately removed 3D density mode (`36013fa`) was a
different model and is unrelated to this duplication.
## Decision
Both loops remain supported and must stay **semantically identical**. The
duplication is historical, not a permanent performance fork. The intended
architecture is one stochastic population step plus an optional xyz overlay;
unification is the long-term direction.
## Consequences
- Apply every event/RNG change to both copies with a parity check.
- Dense arrays are internal; materialize `CellMap`/`Graveyard` before data
  escapes the engine.
- Do not delete either loop ad hoc or add a third variant.
- Tuning constants in either loop are heuristics, not invariants.
## Evidence
To be recorded before this ADR moves to `Accepted` (the `904f5dd` and `36013fa`
commit summaries, plus a parity test).
