# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CellEvoX is a C++23 stochastic simulation system for modeling cellular population dynamics and mutation evolution (e.g., cancer evolution). It uses Gillespie's tau-leap algorithm with a Qt6 QML GUI for visualization.

## Build Commands

The source is under `CellEvoX/` subdirectory; run cmake from the repo root:

```bash
# Standard build (with GUI)
mkdir build && cd build
cmake ../CellEvoX
make -j$(nproc)
./bin/CellEvoX --config ../config.json

# Headless/server build (no Qt6 required)
cmake ../CellEvoX -DSKIP_GUI=ON
make -j$(nproc)
```

## Running Tests

```bash
# Run all tests (excluding benchmarks)
./build/bin/CellEvoXTests "~[benchmark]"

# Run benchmarks
./build/bin/CellEvoXTests "[benchmark]"

# Run with performance regression checking (5% threshold)
python3 CellEvoX/tests/benchmarks/compare_benchmarks.py \
  --binary build/bin/CellEvoXTests \
  --baseline ci_baseline.json \
  --threshold 0.05
```

Tests use Catch2 v3.4.0 (fetched via CMake FetchContent).

## Configuration

Simulations are configured via JSON files (see `config.json`, `example_config.json`):

```json
{
  "stochastic": true,
  "tau_step": 0.005,
  "initial_population": 10000,
  "env_capacity": 10000,
  "steps": 1000000,
  "statistics_resolution": 10,
  "output_path": "./output",
  "mutations": [{ "is_driver": true, "effect": 0.01, "probability": 0.0001, "id": 1 }]
}
```

## Architecture

### Layer Overview

```
Qt6 QML GUI  →  Application Core  →  SimulationEngine  →  ECS Data Layer
```

**ECS Layer** (`include/ecs/`):
- `Cell`: individual entity with fitness, parent-child genealogy, mutation history
- `Run`: complete simulation run — holds concurrent cell population, phylogenetic tree, statistical snapshots

**Simulation Engine** (`include/systems/SimulationEngine.hpp`, `src/systems/SimulationEngine.cpp`):
- Tau-leap stochastic simulation; manages birth/death events with probabilistic mutations
- Uses TBB `concurrent_hash_map` and `concurrent_vector` for thread-safe parallel access
- Tracks a "graveyard" of dead cells to support phylogenetic reconstruction
- Configurable statistical sampling intervals, memory usage logging, graceful SIGTERM handling

**Application Core** (`include/core/`):
- `application`: lifecycle management, Boost.ProgramOptions CLI parsing, JSON config loading
- `RunDataEngine`: CSV export + matplotlib-based plotting (fitness stats, Muller diagrams, phylogenetic trees in DOT/GEXF format)

**GUI** (`CellEvoX/qml/`): Qt6 QML with reusable components under `qml/components/`. QML files are copied to the build directory at configure time.

**Math Utils** (`include/utils/MathUtils.hpp`): Eigen3 + TBB for parallel fitness calculations.

### Key Dependencies

| Library | Purpose |
|---------|---------|
| Qt6 (Quick, QML, Controls2) | GUI |
| Intel TBB | Parallel data structures & algorithms |
| Eigen3 | Linear algebra / fitness math |
| Boost.ProgramOptions | CLI argument parsing |
| spdlog | Structured logging |
| nlohmann_json | Config file parsing |
| matplotlibcpp (vendored) | C++ bindings to Python matplotlib |
| Catch2 v3.4.0 | Test framework (auto-fetched) |

### Python Scripts

`CellEvoX/scripts/` contains standalone analysis tools: `plot_muller.py`, `plot_phylogeny.py`, `plot_clone_counts.py`, etc. These operate on the CSV/DOT/GEXF output produced by `RunDataEngine`.

## Code Style

Uses Google-style clang-format (`.clang-format` in repo root). C++23 with `-O3` globally.
