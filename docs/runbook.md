# Runbook

Canonical commands and environment facts. Rationale for the rules lives in
[AGENTS.md](../AGENTS.md) and [ADRs](adr/README.md).

All examples below are Bash on Linux/macOS. On native Windows, use WSL/Docker
or a Visual Studio build (executables land under `bin/Release`); the benchmark
helpers import the Unix-only `resource` module.

## Build

CMake source directory is `CellEvoX`. Both `CellEvoX` and `CellEvoXTests` are
always defined — there is no GUI option and no Qt dependency.

Prerequisites: a C++23 compiler, CMake >= 3.16, spdlog, Eigen3, TBB,
nlohmann_json >= 3, and Python 3 development headers with NumPy. Catch2 is
fetched by `FetchContent` at configure time.

```bash
cmake -B build -S CellEvoX -DCMAKE_BUILD_TYPE=Release
cmake --build build --target CellEvoX CellEvoXTests -j
```

Portable/Docker builds must disable host-specific SIMD:

```bash
cmake -B build -S CellEvoX -DCMAKE_BUILD_TYPE=Release -DCELLEVOX_NATIVE_OPTIMIZATIONS=OFF
```

Configure requires Python 3 development headers and NumPy. Fresh configure
fetches Catch2 via `FetchContent`, so the first run needs network or a warm cache.

## Test and analyze

```bash
./build/bin/CellEvoXTests "~[benchmark]"
./build/bin/CellEvoX --config quick_config.json
./build/bin/CellEvoX --analyze output/<run_dir>
```

## Benchmarks

```bash
./build/bin/CellEvoXTests "[benchmark]"
python3 CellEvoX/tests/benchmarks/capture_baseline.py --binary build/bin/CellEvoXTests --out ci_baseline.json
python3 CellEvoX/tests/benchmarks/compare_benchmarks.py --binary build/bin/CellEvoXTests --baseline ci_baseline.json --threshold 0.05
```

Optional phase timing: build with `-DCELLEVOX_PROFILE_PHASES=ON` and set
`CELLEVOX_PROFILE_PHASES=<path-or-1>` at runtime.

## Web

Backend and frontend are separate long-running processes. Start each in its own
terminal from the repository root:

```bash
# terminal 1 — backend, starting in the repository root
cd web/backend && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/uvicorn main:app --host 0.0.0.0 --port 7432 --reload

# terminal 2 — frontend, starting in the repository root
cd web/frontend && npm install && npm run dev
```

`web/start.sh` starts both on Unix-like systems, but it only invokes `npm run dev`
— install frontend dependencies once with `npm install` (or let the script's
first run fail and then install them). Backend port `7432`, frontend `5274`.
Point the backend at a specific binary with `CELLEVOX_BINARY`.

## Batch runs

Save this as `batch_manifest.json` in the repository root. Each entry is either
`{"config": {...}}` (an inline config used verbatim) or `{"path": "..."}`, which
loads the whole referenced file. Sibling keys of `path` are ignored — including
`output_path` — so any overrides must live inside the referenced config. `path`
resolves against the manifest's directory (a manifest in `configs/` would need
`../quick_config.json`).

```json
{
  "runs": [
    {"path": "quick_config.json"},
    {"path": "your_experiment.json"}
  ]
}
```

```bash
python3 web/backend/run_batch.py --manifest batch_manifest.json \
  --parallel 4 --threads-per-run 5 --postprocess exports

# equivalent positional form for a few configs
python3 web/backend/run_batch.py quick_config.json your_experiment.json
```

Batches with more than one config are isolated automatically: the runner appends
`/<run_label>` (`<batch_id>_<NNN>`) to each config's `output_path`, and the engine
then creates its own timestamped directory below that. A config without
`output_path` defaults to `./output` in a batch and `./output_<run_label>` for a
single run. There is no per-entry `output_path` override.

For smoke tests use `quick_config.json` (50,000 steps). `example_config.json`
and `config.json` run about a million steps, `example_config.json` has a
placeholder output path, and `test_config.json` still hardcodes a DevContainer
path.

`--threads-per-run` is forwarded as `--threads` and `CELLEVOX_TBB_THREADS`.
Keep `parallel × threads-per-run` near the core count. Use `exports` for
headless batches; `full` adds plots and animations. Resolution fields are in
integer `T` units, not loop steps.

## CI notes

`.github/workflows/ci.yml` runs tests plus a paired base/head benchmark gate
(5% time, 10% memory). Its `-DSKIP_GUI=ON` and Boost installation are dead
leftovers from an older baseline; do not treat them as requirements.
