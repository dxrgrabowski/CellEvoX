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
duplication started as history, but the measurement below shows it is also a
real performance fork, so it cannot be removed by deleting the dense path. The
intended architecture is one stochastic population step plus an optional xyz
overlay; any unification must move the shared step onto dense storage.
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
- Profitability check (local, Release, `-march=native`, 10 samples, one warm-up
  step excluded, five timed steps per sample, same seed and event logic). The
  shared step was run with a prebuilt alive-id cache to match the engine's
  behaviour, and separately without a cache:
  | N / pressure | dense 2D | shared + cached ids | shared, collect ids |
  | --- | --- | --- | --- |
  | 100k low | 0.92 ms | 1.65 ms | 5.38 ms |
  | 100k high | 0.55 ms | 1.62 ms | 7.19 ms |
  | 1M low | 5.49 ms | 16.32 ms | 34.68 ms |
  | 1M high | 8.49 ms | 23.70 ms | 45.93 ms |

  Dense flat storage is 1.8–3x faster than the shared step on `CellMap` with a
  cached alive-id list, and 5–8x faster than the variant that collects ids on
  every step; the gap grows with population. A first timed step without warm-up
  looks ~20x slower because of vector growth, which is why the comparison
  excludes it.
  Conclusion: the duplicate is a real performance fork, not dead code. If it is
  unified, the shared step must move onto dense storage; deleting the dense
  path would regress large 2D runs. The ADR stays `Proposed` until the owner
  signs off on that direction.
- Origin of the duplication could not be recalled by the owner during review;
  it was verified against history instead.
