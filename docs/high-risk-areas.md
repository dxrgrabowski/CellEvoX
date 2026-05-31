# High-Risk Areas

This file lists contracts that are easy to break and the verification expected
before merging changes.

## Risk map

| Area | Files | Why risky | Verification |
| --- | --- | --- | --- |
| Shared population events | `CellEvoX/include/systems/CommonPopulationStep.hpp` | Used by 2D stochastic and 3D capacity; affects ID assignment, deaths, mutations, determinism | Unit tests, 2D/3D capacity parity, benchmarks |
| 2D engine snapshots/logging | `CellEvoX/src/systems/SimulationEngine.cpp` | Snapshot cadence, memory logging, pruning, and progress output can dominate runtime | Unit tests, phase timing/benchmarks for perf changes |
| 3D density model | `CellEvoX/src/systems/SimulationEngine3D.cpp` | Local density, thread-local RNG, spatial hash, mechanics, and ordering interact | 3D determinism/repeatability/stabilization tests |
| 3D capacity model | `CellEvoX/src/systems/SimulationEngine3DCapacity.cpp` | Must preserve 2D event semantics while adding positions and mechanics | 2D parity test and spatial snapshot tests |
| Snapshot format | `CellEvoX/include/io/PopulationSnapshotIO.hpp`, `CellEvoX/scripts/snapshot_io.py` | Binary compatibility must match between C++ and Python | IO tests, Python read smoke, web parser check |
| Output pipeline | `CellEvoX/src/core/RunDataEngine.cpp` | Mixes C++ exports, Python scripts, path lookup, shell commands | RunDataEngine tests, manual analyze smoke for path changes |
| Config schema | `CellEvoX/include/utils/SimulationConfig.hpp`, `web/backend/main.py`, `web/frontend/src/types/simulation.ts` | Drift causes UI to generate configs C++ rejects or misinterprets | Parser tests, backend/frontend schema check, docs update |
| Web runner | `web/backend/runner.py` | Process control, binary discovery, atomic configs, batch behavior | Backend smoke, batch run smoke, path/cwd check |
| Result parser | `web/backend/results_parser.py` | Web charts depend on CSV names, columns, and run discovery | Endpoint smoke with representative output |
| CMake/dependencies | `CellEvoX/CMakeLists.txt`, `.github/workflows/ci.yml` | GUI/test split, FetchContent, CI benchmark behavior | Clean configure/build in intended environment |

## CommonPopulationStep invariants

`applyCommonPopulationStep` is performance-critical and correctness-critical.
Preserve these contracts unless intentionally changing model behavior:

- Alive IDs are collected and sorted before indexed RNG vectors are consumed.
- New IDs are assigned deterministically from `N + total_deaths`.
- Births are sorted before ID assignment.
- Deaths are sorted before application.
- Parent death and daughter creation semantics remain consistent.
- Mutation placeholder ID `0` is replaced with the daughter ID.
- `actual_population` and `total_deaths` stay coherent with `cells` and `graveyard`.
- 3D capacity can use returned birth events to place daughters by parent.

## Snapshot format invariants

- Keep packed struct sizes stable unless bumping format/version intentionally.
- Update C++ reader/writer and Python loader together.
- Maintain backward readers for old files when possible.
- Preserve `spatial_dimensions` and `position_valid` semantics.
- Preserve driver-only vs full mutation payload flags.
- Tests should write real files and read them back.

## Determinism invariants

- Seeded 2D stochastic runs should reproduce stats.
- 3D capacity should match 2D population events when spatial mechanics are not
  part of the comparison.
- 3D density deterministic tests currently use single-thread TBB control where
  needed. Do not generalize those guarantees without evidence.
- Sorting and ID assignment are part of deterministic behavior.

## Performance hazards

Low CPU utilization does not automatically mean the parallel loop is bad. It can
also mean the code is blocked on memory bandwidth, allocator overhead, map/cache
misses, sorting, or IO.

Known suspects:

- `logMemoryUsage()` flushes `memory_log.csv`; frequent flushes can reduce CPU
  utilization and dominate short steps.
- `CommonPopulationStep` has sequential RNG, alive-ID collection, sorting, merge,
  and map mutation around a parallel loop.
- `tbb::concurrent_hash_map` lookups and erases can be cache/memory bound.
- `Cell::mutations` is heap-backed and copied on birth.
- Population snapshots copy or serialize many cells.
- `Run` construction and phylogeny processing happen after simulation and can
  look like poor simulation scaling if measured coarsely.

Amdahl gate:

```text
maximum_speedup = 1 / sequential_fraction
```

If measurement shows 25 percent sequential/IO time, no amount of worker threads
can produce more than 4x total speedup without reducing that 25 percent.

## Config drift risks

Whenever adding or changing config fields, update all surfaces:

- C++ parser and defaults,
- backend `/api/config/schema`,
- frontend TypeScript type,
- frontend defaults,
- frontend export/import cleanup,
- examples/config files if relevant,
- [Config fields by mode](config-fields-by-mode.md).

Current known drift/caveat:

- `deterministic` is selectable/configurable but not implemented in current C++
  step dispatch.
- Backend accepts arbitrary config dictionaries; C++ parser is the real required
  field gate.

## Path and platform risks

- Some code reads `/proc/self/statm` and includes `unistd.h`, so native Windows
  builds need portability work.
- Several paths assume Unix-like shell/Python behavior.
- Some older `RunDataEngine` script commands do not quote paths.
- CI is Ubuntu-based; local Windows success alone does not prove CI success, and
  CI success alone does not prove native Windows portability.

## Merge checklist for risky changes

1. Identify affected contract from this document.
2. Update or add a targeted test.
3. Run `CellEvoXTests "~[benchmark]"`.
4. For performance work, run the relevant benchmark before and after.
5. Inspect generated output if output files changed.
6. Update docs in the same branch.
