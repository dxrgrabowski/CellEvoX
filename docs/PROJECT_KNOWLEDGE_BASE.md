# CellEvoX Project Knowledge Base

This is the compact briefing for agents and contributors. Use it after
[Agent onboarding](agent-onboarding.md), then follow links to the topic docs when
you need details.

## Project purpose

CellEvoX models cell population dynamics with stochastic birth/death, mutation
accumulation, clone tracking, phylogeny export, and optional 3D spatial mechanics.
It produces statistics, binary and CSV population snapshots, plots, phylogeny
files, Muller diagrams, and replay animations.

The current production center is the C++ CLI simulation plus Python
post-processing and the local web control/results surface.

## Repository shape

Top-level map:

- `CellEvoX/` - active C++ project root, QML files, tests, and Python scripts.
- `CellEvoX/CMakeLists.txt` - active CMake entry point.
- `web/backend/` - FastAPI bridge, subprocess runner, results parser.
- `web/frontend/` - React/Vite frontend.
- `docs/` - project knowledge layer.
- `.github/workflows/ci.yml` - Ubuntu test and benchmark-regression workflow.
- `assets/` - README demo media.
- `config.json`, `quick_config.json`, `test_config.json`, `example_config.json` - sample configs.

## Build and test

Normal test build:

```bash
cmake -B build -S CellEvoX -DCMAKE_BUILD_TYPE=Release -DSKIP_GUI=ON
cmake --build build --target CellEvoXTests -j
./build/bin/CellEvoXTests "~[benchmark]"
```

Benchmarks:

```bash
./build/bin/CellEvoXTests "[benchmark]"
```

Full executable:

- `SKIP_GUI=ON` builds tests but not the runnable `CellEvoX` binary.
- Building `CellEvoX` requires GUI/Qt dependencies.
- The web backend can use `CELLEVOX_BINARY` to point at a known executable.

## Runtime modes

Public config mode strings:

- `stochastic`
- `deterministic`
- `spatial_3d_density`
- `spatial_3d_capacity`

Implemented engine paths:

- `stochastic` -> `SimulationEngine`
- `spatial_3d_density` -> `SimulationEngine3D`
- `spatial_3d_capacity` -> `SimulationEngine3DCapacity`

Current caveat:

- `deterministic` maps to `DETERMINISTIC_RK4`, but current mainline
  `SimulationEngine::step()` does not dispatch an RK4 implementation.

## Core data model

Important C++ types:

- `MutationType`: effect, probability, type ID, driver/passenger flag.
- `Cell`: parent ID, ID, fitness, death time, and mutation vector.
- `CellMap`: `tbb::concurrent_hash_map<uint32_t, Cell>`.
- `Graveyard`: dead-cell ancestry map from cell ID to parent/death time.
- `SimulationConfig`: parsed config fields and defaults.
- `StatSnapshot`: per-generation summary statistics.
- `ecs::Run`: final state plus derived summaries/phylogeny.

## Engine summary

2D stochastic:

- Uses global carrying capacity `env_capacity`.
- Calls `applyCommonPopulationStep`.
- Writes non-spatial population snapshots.
- Main hotspot for performance work.

3D density:

- Uses local density within `sample_radius`.
- Maintains persistent 3D positions and a spatial hash grid.
- Applies local crowding to death/birth rates.
- Runs mechanical relaxation.

3D capacity:

- Reuses the same population-event logic as 2D stochastic.
- Adds 3D daughter placement and mechanics after common events.
- Must preserve 2D population-event parity.

## Output summary

Typical run output:

- `config.json`
- `statistics/generational_statistics.csv`
- `statistics/memory_log.csv`
- `population_data/population_generation_<generation>.bin`
- `population_data/population_generation_<generation>.csv`
- `phylogeny/phylogenetic_tree.csv`
- `phylogeny/phylogenetic.gexf`
- plot directories and visualization media

Snapshot binary format is defined in
`CellEvoX/include/io/PopulationSnapshotIO.hpp` and mirrored in
`CellEvoX/scripts/snapshot_io.py`.

## Web summary

Backend:

- FastAPI on port `7432`.
- `/api/config/schema` publishes config metadata.
- `/api/simulation/start` and `/api/simulation/batch/start` run simulations.
- `/ws/logs` streams process logs.
- `/api/results/*` exposes parsed outputs.

Frontend:

- React/Vite on port `5274`.
- Configure, Run, and Results pages.
- Batch queue is stored in browser state and launched sequentially through the backend.

## High-risk facts

- `CommonPopulationStep` is shared by 2D stochastic and 3D capacity.
- Snapshot binary changes require C++, Python, tests, and web parsing alignment.
- `RunDataEngine` path and shell command handling is integration-sensitive.
- Memory logging can distort performance measurements.
- Native Windows portability is incomplete because of `/proc/self/statm` and
  `unistd.h` assumptions.
- Deterministic mode is not currently implemented despite being visible in config surfaces.

## Best next docs

- [Architecture](architecture.md)
- [Simulation engines](simulation-engines.md)
- [Config fields by mode](config-fields-by-mode.md)
- [Run output and analysis](run-output-and-analysis.md)
- [Development workflows](development-workflows.md)
- [High-risk areas](high-risk-areas.md)
