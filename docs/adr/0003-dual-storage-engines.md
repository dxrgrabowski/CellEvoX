# ADR-0003: Dense 2D storage and the duplicated event loop
Status: Accepted — debt acknowledged · 2026-10-24

## Context
The stochastic simulator came first on concurrent hash maps. A density-based
model later added dense arrays and a spatial layer; that model was removed,
leaving two copies of the same event loop (dense 2D versus the shared step used
by 3D).
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
