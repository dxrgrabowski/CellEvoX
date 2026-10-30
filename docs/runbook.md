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

### Native build outside the DevContainer

Outside the DevContainer, point CMake at the interpreter the binary should embed;
a local `.tools/env` Python 3.12 toolchain works:

```bash
.tools/env/bin/python -m venv --system-site-packages .venv
.venv/bin/pip install -r CellEvoX/scripts/requirements-visualization.txt
cmake -B build -S CellEvoX -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
```

## Runtime environment

Post-processing needs Python `matplotlib`/`numpy`/`pandas`/`networkx` and
`pyfish` (`CellEvoX/scripts/requirements-visualization.txt`); `ffmpeg` enables
MP4 output (GIF otherwise); PyVista is optional for 3D replay. Set
`MPLCONFIGDIR` in headless containers.

`CELLEVOX_TBB_THREADS` and `--threads` cap TBB inside a process. Keep the number
of concurrent runs times threads per run near the core count; oversubscription
dominates any gain.

`statistics/memory_log.csv` is sampled from `/proc/self/statm`, so memory
reporting is Linux-only and adds I/O; avoid frequent flushes in performance runs.

The binary locates plot scripts relative to the source tree and runs the Python
interpreter found at configure time; override it at runtime with
`CELLEVOX_PYTHON`.

## CLI

`CellEvoX` needs exactly one of `--config` or `--analyze`; the rest is optional.

| Flag | Effect |
| --- | --- |
| `--config <path>` | Run the simulation described by a JSON config. |
| `--analyze <path>` | Re-run post-processing on an existing run directory. |
| `--threads <n>` | Cap TBB parallelism; overrides `CELLEVOX_TBB_THREADS`. |
| `--postprocess <full\|exports>` | Post-processing set; defaults to `full`. |
| `--help`, `-h` | Print usage. |

`exports` writes the CSV/GEXF result files without plots. `csv` is accepted as
an alias for `exports` but is not listed in `--help`. Unknown options print
usage and exit with status 1. `full` needs the Python plotting dependencies from
[Runtime environment](#runtime-environment); use `exports` for a headless first
result.

## First experiment

`quick_config.json` is the short reference run: 10,000 steps at `tau_step 0.005`
(50 generations), with one statistics row and one population snapshot per
generation.

```json
{
  "simulation_mode": "stochastic",
  "seed": 42,
  "tau_step": 0.005,
  "initial_population": 1000,
  "env_capacity": 1000,
  "steps": 10000,
  "statistics_resolution": 1,
  "population_statistics_res": 1,
  "output_path": "./output",
  "mutations": [
    {"is_driver": true, "effect": 0.01, "probability": 0.0001, "id": 1},
    {"is_driver": false, "effect": -0.01, "probability": 0.01, "id": 3}
  ]
}
```

Required keys are `tau_step`, `initial_population`, `env_capacity`, `steps`,
`statistics_resolution`, `population_statistics_res`, `output_path` and
`mutations`; `seed` defaults to `42`. `simulation_mode` also accepts
`spatial_3d_capacity`. The removed `deterministic` mode (and legacy
`stochastic: false`) is rejected with an error naming the supported modes.
`verbosity` sets the log level: `0` is
off, `1` warnings only, `2` (default) info. The legacy boolean `stochastic: true`
is still accepted with a warning, while `log_level` is ignored.

Run it with exports only:

```bash
./build/bin/CellEvoX --config quick_config.json --postprocess exports
```

Every run writes a timestamped directory below `output_path`; find the newest:

```bash
run_dir=$(ls -dt output/*/ | head -n 1)
```

Files worth opening, in order:

- `run_metadata.json` — `termination_reason`, `terminal_time` and
  `terminal_population`; this is the authoritative terminal state.
- `population_data/population_generation_<N>.bin` — snapshots written during the
  run; `exports` adds `<N>.csv` companions next to them. Columns and pitfalls
  are in [output-contract.md](output-contract.md#population-csv).
- `statistics/generational_statistics.csv` — fitness and mutation moments per
  sampled generation (`Generation` stores tau).
- `phylogeny/phylogenetic.gexf` and `phylogenetic_tree.csv` — the tree with
  `ALIVE`/`DEAD` `status`, written by `exports`.
- `config.json` — the effective configuration copied into the run directory.

Regenerate every plot and animation from that directory:

```bash
./build/bin/CellEvoX --analyze "$run_dir"
```

This writes `muller_plots/`, `general_plots/`, `mutation_histograms/`,
`vaf_diagrams/` and `visualizations/` inside the run directory. `--analyze` reads
CSV snapshots, and exports CSV companions from `.bin` files when no CSV exists.

## Tests

```bash
./build/bin/CellEvoXTests "~[benchmark]"
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
first run fail and then install them). Backend port `7432`, frontend `5274`, and
the CORS allowlist is fixed. Point the backend at a specific binary with
`CELLEVOX_BINARY`. The backend runs one simulation job at a time and answers HTTP
409 otherwise. Its HTTP surface is `/api/config/schema`, `/api/simulation/*`,
`/api/results/*`, and the `/ws/logs` WebSocket; `web/backend/main.py` defines the
payloads.

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
    {
      "config": {
        "simulation_mode": "stochastic",
        "tau_step": 0.005,
        "initial_population": 200,
        "env_capacity": 200,
        "steps": 1000,
        "statistics_resolution": 1,
        "population_statistics_res": 1,
        "output_path": "./output",
        "mutations": [
          {"is_driver": true, "effect": 0.01, "probability": 0.0001, "id": 1}
        ]
      }
    }
  ]
}
```

```bash
python3 web/backend/run_batch.py --manifest batch_manifest.json \
  --parallel 2 --threads-per-run 5 --postprocess exports

# positional form: each argument is a config path
python3 web/backend/run_batch.py quick_config.json
```

Batches with more than one config are isolated automatically: the runner appends
`/<run_label>` (`<batch_id>_<NNN>`) to each config's `output_path`, and the engine
then creates its own timestamped directory below that. A config without
`output_path` defaults to `./output` in a batch and `./output_<run_label>` for a
single run. There is no per-entry `output_path` override.

For smoke tests use `quick_config.json` (10,000 steps). `example_config.json`
and `config.json` run about a million steps; `example_config.json` has a
placeholder output path.

`--threads-per-run` is forwarded as `--threads` and `CELLEVOX_TBB_THREADS`. Use
`exports` for headless batches; `full` adds plots and animations. Resolution
fields are in integer `T` units, not loop steps.

## CI notes

`.github/workflows/ci.yml` runs tests plus a paired base/head benchmark gate
(5% time, 10% memory). Its `-DSKIP_GUI=ON` flag is a dead leftover from an
older baseline; do not treat it as a requirement.

The gate interleaves base and head (7 rounds x 5 samples, order flipped every
round, retry at 10 samples per round) and judges a benchmark on the median of
the round means, so a single noisy round widens the interval instead of moving
the result. Reproduce a failure locally with the same flags; `--rounds 1` runs
the older blocked schedule. Rationale: ADR-0009.

`.github/workflows/ai-review.yml` runs the advisory AI review
(`tools/ai_review.py`, DeepSeek via OpenRouter, prompt `v4`) on non-draft pull
requests from this repository and rewrites one sticky PR comment on every push.
It needs the repository secret `OPENROUTER_API_KEY`; fork PRs are skipped
because secrets are not exposed to them. The job fails only when the review
cannot run (missing key, API error, unparseable response) — findings never block
the merge. Run it locally against the same diff:

```bash
OPENROUTER_API_KEY=... python3 tools/ai_review.py \
  --range origin/main...HEAD --prompt v4 --reasoning off
```

`.github/workflows/review-benchmark.yml` puts that review next to
[Robin](https://github.com/antongulin/robin) and
[PR-Agent](https://github.com/The-PR-Agent/pr-agent) on the same pull request,
and `tools/review_bench.py` scores what each one posted against the labels of a
case in `bench/cases/`. The benchmark is skipped unless the PR has a case:
unlabelled findings cannot be scored, and every bot run costs money.

A case is a JSON file with `id`, `repo` (`owner/name`), `pr`, `source` and
`labels`. Each label is a `path` plus either a `lines` region (derived from the
fix commit, no tolerance) or a `line` (default tolerance 3), and a `kind` of
`real`, `fp` or `unverified`. Scoring is the same `score()` the local eval uses,
so a label means the same thing in both places.

Cost is what a tool publishes about its own run, never an assumption. PR-Agent
reports tokens and an estimated cost when its workflow enables
`config.output_run_details` and `config.output_run_cost`; Robin reports neither
and is always `unpriced`; ours publishes a token marker, which
`--price-in/--price-out` (USD per million tokens) turns into dollars, normally
supplied as `vars.REVIEW_PRICE_IN` / `vars.REVIEW_PRICE_OUT`. A tool with no
published usage shows no price instead of `$0`.

```bash
python3 tools/review_bench.py case --pr 50
python3 tools/review_bench.py collect --pr 50 --price-in 0.14 --price-out 0.28
python3 tools/review_bench.py report
```

Robin needs the secrets `LLM_API_KEY`, `LLM_BASE_URL` and `LLM_MODEL`; our
reviewer and PR-Agent both use `OPENROUTER_API_KEY`. The parsers are covered by
`python3 tools/test_review_bench.py`, which the `review-tooling` job of
`.github/workflows/quality-gates.yml` runs.
