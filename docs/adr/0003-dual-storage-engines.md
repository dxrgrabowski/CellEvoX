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
- `SimulationEngine::stochasticDenseStep()` is the **live** 2D path, not dead
  code. Removing it means porting its performance behaviour to the shared step,
  not deleting an unused branch.

## Evidence
- `904f5dd` "Optimize stochastic simulation scaling" introduced the dense
  storage and the second loop; `36013fa` "remove redundant 3D density simulation
  mode" removed a different model and is not the cause of the duplication.
- Live call site: `CellEvoX/src/systems/SimulationEngine.cpp` (`step()` calls
  `stochasticDenseStep()`), implementation immediately below it.
- Parity: `CellEvoX/tests/test_simulation.cpp`, "SimulationEngine3DCapacity
  matches 2D population events".
- Origin of the duplication could not be recalled by the owner during review;
  it was verified against history instead. The ADR stays `Proposed` until the
  unification/parity decision is signed off.
