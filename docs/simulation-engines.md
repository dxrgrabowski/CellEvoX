# Simulation Engines

CellEvoX currently has three implemented simulation paths and one parsed but
unimplemented deterministic mode. This document records semantics, shared code,
and correctness gates.

## Engine overview

| Mode | Engine | Population regulation | Spatial state | Main use |
| --- | --- | --- | --- | --- |
| `stochastic` | `SimulationEngine` | Global carrying capacity through `env_capacity` | None | Classic 2D/non-spatial stochastic population dynamics |
| `spatial_3d_density` | `SimulationEngine3D` | Local density within `sample_radius` | Persistent positions plus spatial hash grid | Tumor-like 3D growth with local crowding |
| `spatial_3d_capacity` | `SimulationEngine3DCapacity` | Same global event model as 2D stochastic | Persistent positions plus spatial hash grid | 3D geometry while preserving 2D event semantics |
| `deterministic` | Parsed as `DETERMINISTIC_RK4` | Not active in current dispatch | None | Placeholder/status only on current mainline |

## 2D stochastic engine

Files:

- `CellEvoX/include/systems/SimulationEngine.hpp`
- `CellEvoX/src/systems/SimulationEngine.cpp`
- `CellEvoX/include/systems/CommonPopulationStep.hpp`

Runtime flow:

1. Initialize live cells with IDs `[0, initial_population)`.
2. Store mutation types in `available_mutation_types`.
3. Each stochastic step increments `tau` by `tau_step`.
4. `applyCommonPopulationStep` computes deaths, births, and mutations.
5. Snapshot/statistics/pruning/memory logging run on configured tau intervals.
6. `run()` returns an `ecs::Run` owning final state and reports.

`CommonPopulationStep` does important sequential and parallel work:

- Generate death random variables.
- Collect and sort alive IDs.
- Build the fitness vector.
- Generate birth random variables.
- Generate mutation random values.
- Run TBB `parallel_for` over alive IDs.
- Copy and sort births.
- Sort deaths.
- Apply births to `CellMap`.
- Apply deaths to `Graveyard` and erase live cells.

This is the main hotspot for 2D performance analysis. The parallel loop is only
one phase. RNG, sorting, merging, map mutation, snapshots, and memory logging can
limit scaling under Amdahl's law.

## 3D density engine

Files:

- `CellEvoX/include/systems/SimulationEngine3D.hpp`
- `CellEvoX/src/systems/SimulationEngine3D.cpp`
- `CellEvoX/include/spatial/SpatialHashGrid.hpp`
- `CellEvoX/src/spatial/SpatialHashGrid.cpp`

Runtime behavior:

- Initializes cells on a jittered 3D lattice.
- Maintains ID-indexed position arrays and a compact active `SpatialState`.
- Rebuilds a spatial hash grid for radius queries.
- Computes local density from neighbors within `sample_radius`.
- Splits crowding pressure between death and birth rates.
- Uses thread-local RNG seeded from the engine seed and TBB thread index.
- Places daughters symmetrically around the parent.
- Runs mechanical relaxation with overlap forces after event application.
- Writes spatial binary snapshots with `spatial_dimensions == 3`.

Correctness risks:

- Parallel RNG and TBB scheduling can affect repeatability.
- Spatial hash rebuild/query contracts must match position vectors.
- Density and mechanics parameters are coupled; small rate changes can alter
  stabilization tests.

## 3D capacity engine

Files:

- `CellEvoX/include/systems/SimulationEngine3DCapacity.hpp`
- `CellEvoX/src/systems/SimulationEngine3DCapacity.cpp`
- `CellEvoX/include/systems/CommonPopulationStep.hpp`

Runtime behavior:

- Calls `applyCommonPopulationStep` for birth/death/mutation events.
- Uses a separate spatial RNG for daughter placement.
- Rebuilds spatial state and runs mechanical relaxation after common events.
- Writes spatial binary snapshots with `spatial_dimensions == 3`.

The key invariant is population-event parity with 2D stochastic under the same
seed/config. Spatial placement must not change the shared event results.

## Deterministic mode status

The config surface includes `deterministic`, and
`SimulationConfig::fromJson()` maps it to `DETERMINISTIC_RK4`. On the current
mainline described by these docs, `SimulationEngine::step()` only dispatches
`STOCHASTIC_TAU_LEAP`; the deterministic branch is commented out. Treat
deterministic mode as unavailable until the C++ step and tests exist on the
branch you are working from.

Any future deterministic implementation should update:

- `SimulationEngine::step()` dispatch,
- deterministic engine state and output semantics,
- frontend/backend labels if behavior differs from stochastic,
- `docs/config-fields-by-mode.md`,
- this document,
- correctness tests for numeric stability and expected outputs.

## Correctness gates

Use these tests as the first line of defense:

- `Simulation Determinism`
- `SimulationEngine3D deterministic smoke test`
- `SimulationEngine3D is repeatable with the same seed`
- `SimulationEngine3DCapacity matches 2D population events`
- snapshot IO tests under `[PopulationSnapshotIO]`

Run all non-benchmark tests after behavior changes:

```bash
./build/bin/CellEvoXTests "~[benchmark]"
```

For changes touching performance-sensitive paths, run benchmarks before and
after:

```bash
./build/bin/CellEvoXTests "[benchmark]"
```

## Performance notes

Do not expect 100 percent CPU utilization to prove or disprove good scaling. The
hot path includes memory-bound map lookups, heap-backed mutation vectors, sorting,
serial RNG phases, snapshot IO, and memory logging.

For 2D stochastic optimization, measure phases before rewriting:

- RNG death and birth generation,
- alive-ID collection/sort,
- fitness vector build,
- mutation RNG,
- parallel event loop,
- birth/death sorting and application,
- statistics snapshots,
- population snapshots,
- memory logging,
- `Run` post-processing.

If a profile shows 25 percent sequential or IO time, Amdahl's law caps speedup at
`1 / 0.25 = 4x` no matter how many cores are available.
