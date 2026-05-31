# Development Workflows

Use this document for day-to-day commands, verification gates, and performance
work discipline.

## Branch and workspace hygiene

Before changing code:

```bash
git status --short --branch
git log -1 --oneline --decorate
```

Do not commit local IDE artifacts such as `.vs/`. If the worktree already has
unrelated changes, preserve them and keep your commit scoped.

## C++ test build

The active CMake source directory is `CellEvoX`.

```bash
cmake -B build -S CellEvoX -DCMAKE_BUILD_TYPE=Release -DSKIP_GUI=ON
cmake --build build --target CellEvoXTests -j
./build/bin/CellEvoXTests "~[benchmark]"
```

Notes:

- `SKIP_GUI=ON` avoids Qt requirements and builds `CellEvoXTests`.
- `SKIP_GUI=ON` does not build the runnable `CellEvoX` executable.
- Fresh configure uses CMake `FetchContent` for Catch2, so network/cache state can
  affect first setup.

## Full executable

The web runner needs a `CellEvoX` executable. Build without `SKIP_GUI=ON` when Qt
dependencies are available, or point the backend to a known binary:

```bash
set CELLEVOX_BINARY=C:\path\to\CellEvoX.exe
```

On Unix-like shells:

```bash
export CELLEVOX_BINARY=/path/to/CellEvoX
```

## Running the CLI

```bash
./build/bin/CellEvoX --config quick_config.json
./build/bin/CellEvoX --analyze output/2026-05-31_22-30-00
```

If using the backend, launch configs are written under `.cellevox_launch_configs`
and passed to the binary with `--config`.

## Web development

Backend:

```bash
cd web/backend
python -m venv .venv
.venv/Scripts/pip install -r requirements.txt
.venv/Scripts/uvicorn main:app --host 0.0.0.0 --port 7432 --reload
```

Frontend:

```bash
cd web/frontend
npm install
npm run build
npm run dev
```

The frontend dev server uses port `5274`; the backend uses port `7432`.

`web/start.sh` automates both for Unix-like environments.

## Benchmarks

Benchmarks live in `CellEvoX/tests/bench_simulation.cpp`.

Run all benchmark cases:

```bash
./build/bin/CellEvoXTests "[benchmark]"
```

Capture/compare baseline:

```bash
python3 CellEvoX/tests/benchmarks/capture_baseline.py --binary build/bin/CellEvoXTests --out ci_baseline.json
python3 CellEvoX/tests/benchmarks/compare_benchmarks.py --binary build/bin/CellEvoXTests --baseline ci_baseline.json --threshold 0.05
```

Performance rule: every performance change needs benchmark evidence. Run the
relevant benchmark before the change and after the change, using comparable build
type, compiler, machine, and config. Record the benchmark names and observed
direction, not just "seems faster".

## Performance investigation workflow

Use this order for 2D stochastic work:

1. Establish benchmark baseline.
2. Add or enable phase timing without semantic changes.
3. Measure phase shares for representative sizes and IO settings.
4. Apply Amdahl's law to decide whether parallelizing one phase can matter.
5. Change one bottleneck at a time.
6. Re-run correctness tests.
7. Re-run the same benchmark.
8. Report speedup, memory effect, and any determinism impact.

Representative scenarios:

- `N=100k` and `N=1M`,
- low and high death pressure,
- mutations off and on,
- snapshots off and on,
- memory logging on and throttled/off.

Priority suspects for the 2D path:

- memory logging flush frequency,
- sequential RNG generation,
- alive-ID sorting,
- fitness-vector gather from `CellMap`,
- `tbb::concurrent_vector` merge and sorting,
- insertion/erase costs in `tbb::concurrent_hash_map`,
- heap allocation in per-cell mutation vectors,
- population snapshots and `Run` post-processing.

## CI

`.github/workflows/ci.yml` runs on Ubuntu 24.04 for pushes/PRs to `main`.

It:

- installs C++/Python dependencies,
- configures with `-DSKIP_GUI=ON`,
- builds `CellEvoXTests`,
- runs non-benchmark tests,
- compares benchmarks against a cached or freshly built base baseline with a 5
  percent threshold.

## Documentation updates

Update docs in the same branch when changing:

- config fields or defaults,
- engine mode availability,
- snapshot or output formats,
- benchmark workflow,
- web launch/results behavior,
- build requirements,
- known caveats or high-risk files.

The docs are intended to be an operational memory for agents. Stale docs are worse
than missing docs because they create confident wrong turns.
