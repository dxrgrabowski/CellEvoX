# Runbook

Canonical commands and environment facts. Rationale for the rules lives in
[AGENTS.md](../AGENTS.md) and [docs/adr/](README.md).

## Build

CMake source directory is `CellEvoX`. Both `CellEvoX` and `CellEvoXTests` are
always defined — there is no GUI option and no Qt dependency.

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

```bash
cd web/backend && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/uvicorn main:app --host 0.0.0.0 --port 7432 --reload
cd web/frontend && npm install && npm run dev
```

`web/start.sh` starts both on Unix-like systems. Backend port `7432`, frontend
`5274`. Point the backend at a specific binary with `CELLEVOX_BINARY`.

## Batch runs

```bash
python3 web/backend/run_batch.py --manifest batch_manifest.json \
  --parallel 4 --threads-per-run 5 --postprocess exports
```

`--threads-per-run` is forwarded as `--threads` and `CELLEVOX_TBB_THREADS`.
Keep `parallel × threads-per-run` near the core count. Use `exports` for
headless batches; `full` adds plots and animations. Resolution fields are in
integer `T` units, not loop steps.

## CI notes

`.github/workflows/ci.yml` runs tests plus a paired base/head benchmark gate
(5% time, 10% memory). Its `-DSKIP_GUI=ON` and Boost installation are dead
leftovers from an older baseline; do not treat them as requirements.
