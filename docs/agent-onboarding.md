# Agent Onboarding

This is the first document to read when you inherit a CellEvoX thread. It is
optimized for getting useful quickly and avoiding the most expensive wrong
assumptions.

## First 30 minutes

1. Check branch and dirty state:

   ```bash
   git status --short --branch
   git log -1 --oneline --decorate
   ```

2. Inspect the doc map:

   ```bash
   rg --files docs
   ```

3. Know the build root:

   ```bash
   cmake -B build -S CellEvoX -DCMAKE_BUILD_TYPE=Release -DSKIP_GUI=ON
   cmake --build build --target CellEvoXTests -j
   ./build/bin/CellEvoXTests "~[benchmark]"
   ```

4. For performance work, capture baseline benchmarks before editing:

   ```bash
   ./build/bin/CellEvoXTests "[benchmark]"
   ```

5. For web work, remember that the backend expects a runnable C++ binary:

   ```bash
   cd web/frontend
   npm install
   npm run build
   ```

   Backend runtime is under `web/backend`. The runner searches `build/bin/CellEvoX`,
   `CellEvoX/build/bin/CellEvoX`, or `CELLEVOX_BINARY`.

## Mental model

CellEvoX has four active layers:

- C++ simulation core under `CellEvoX/`.
- Python post-processing scripts under `CellEvoX/scripts/`.
- FastAPI backend plus React frontend under `web/`.
- Documentation and operational knowledge under `docs/`.

The current production center is the CLI simulation and output pipeline. The QML
GUI files exist, but the normal tested path is not QML-driven.

## What is implemented

- `stochastic`: classic 2D/non-spatial stochastic tau-leap engine.
- `spatial_3d_density`: 3D local-density model with spatial hash and mechanics.
- `spatial_3d_capacity`: 3D geometry around the same global population-event
  semantics as the 2D stochastic engine.

Current caveat: `deterministic` is visible in config/frontend/backend and parsed
as `DETERMINISTIC_RK4`, but the current mainline `SimulationEngine::step()` does
not dispatch a deterministic RK4 step.

## Safe work rules

- Treat `CellEvoX/CMakeLists.txt` as the active CMake entry point. There is no
  active root `CMakeLists.txt`.
- Do not assume `SKIP_GUI=ON` builds the runnable `CellEvoX` executable. It builds
  `CellEvoXTests`.
- Do not include local IDE state such as `.vs/` in commits.
- Do not change stochastic or spatial semantics without a correctness test that
  detects the behavior.
- Do not change performance-sensitive code without benchmark evidence before and
  after the change.
- Do not change snapshot binary layout unless C++ IO, Python loader, and tests
  all move together.

## Where to look

| Area | Files |
| --- | --- |
| Config parsing | `CellEvoX/include/utils/SimulationConfig.hpp` |
| Engine selection | `CellEvoX/src/core/application.cpp` |
| 2D stochastic engine | `CellEvoX/include/systems/SimulationEngine.hpp`, `CellEvoX/src/systems/SimulationEngine.cpp` |
| Shared population step | `CellEvoX/include/systems/CommonPopulationStep.hpp` |
| 3D density engine | `CellEvoX/include/systems/SimulationEngine3D.hpp`, `CellEvoX/src/systems/SimulationEngine3D.cpp` |
| 3D capacity engine | `CellEvoX/include/systems/SimulationEngine3DCapacity.hpp`, `CellEvoX/src/systems/SimulationEngine3DCapacity.cpp` |
| Snapshot binary format | `CellEvoX/include/io/PopulationSnapshotIO.hpp`, `CellEvoX/scripts/snapshot_io.py` |
| Output/export pipeline | `CellEvoX/include/core/RunDataEngine.hpp`, `CellEvoX/src/core/RunDataEngine.cpp` |
| Web backend | `web/backend/main.py`, `web/backend/runner.py`, `web/backend/results_parser.py` |
| Web frontend config/results | `web/frontend/src/pages`, `web/frontend/src/components`, `web/frontend/src/types/simulation.ts` |
| Correctness tests | `CellEvoX/tests/test_simulation.cpp` |
| Benchmarks | `CellEvoX/tests/bench_simulation.cpp` |

## Change protocols

Simulation behavior:

- Identify the affected engine.
- Find whether the behavior is shared by 2D and 3D capacity through
  `CommonPopulationStep`.
- Add or update a test that compares final stats, cell map, graveyard, and
  snapshot behavior as appropriate.
- Run `CellEvoXTests "~[benchmark]"`.

Performance behavior:

- Run the relevant benchmark before editing.
- Change one bottleneck at a time.
- Run the same benchmark after editing.
- Keep correctness tests green.
- Report speedup, memory effect, and any Amdahl limit inferred from phase timing.

Config behavior:

- Update C++ parser.
- Update backend schema.
- Update frontend types/defaults/export/import payload cleanup.
- Update [Config fields by mode](config-fields-by-mode.md).

Output behavior:

- Update C++ writer/reader.
- Update Python mirror loader or results parser.
- Update tests that read real files.
- Update [Run output and analysis](run-output-and-analysis.md).

## Red flags

- A mode appears in the web schema but is not implemented in C++ dispatch.
- A benchmark shows low CPU utilization while output or memory logging is enabled.
- A change to `CommonPopulationStep` breaks 3D capacity parity with 2D.
- A snapshot test passes in C++ but Python results parsing silently drops fields.
- A path with spaces works for new animation scripts but fails in older `std::system`
  calls that are not quoted.
