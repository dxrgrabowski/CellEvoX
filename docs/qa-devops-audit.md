# CellEvoX — QA / DevOps / Quality-Gate Audit

**Scope:** repository at `/home/dxr/rep/CellEvoX` as of `1aa36a0`, audited 2026-10-26. Gaps are described in their pre-fix state; the companion PR fixes some of them rather than editing this snapshot.
**Goal:** prevent regressions and raise confidence before merge/deploy.
**Method:** static inspection of source, tests, CMake, GitHub Actions, Dockerfile, web backend/frontend, and the documented invariants in `AGENTS.md` / `docs/`.

---

## 0. Executive summary

CellEvoX has a **strong core of deterministic C++ engine tests** (45 Catch2 cases, golden baselines, 2D/3D parity checks) and a **well-designed paired performance gate**. That is substantially better than most research simulators.

The quality system is nevertheless **narrow and single-layer**:

- All automated verification is concentrated on the C++ engine. The **web backend (1 600+ LOC), frontend (3 200+ LOC), Python analysis/plotting modules, and the cross-language snapshot reader are untested**, even though they consume the documented public output contract.
- CI runs **one build configuration** (Release), **no sanitizers**, **no coverage gate**, **no `ctest`**, **no clang-tidy/format check**, **no SAST/dependency scanning**, and **no pre-commit hooks**.
- The **Docker image is published independently of test success** and is a *dev environment*, not a built simulator.
- Several **documented contract drifts are already present in untested code** (dead `deterministic` mode advertised by the web API, `csv` postprocess accepted by the runner but rejected by the API, CSV-only results parsing vs. binary snapshots as source of truth).

**Overall verdict:** high confidence in the simulation core; **low confidence in the boundaries where data changes hands** (C++ → Python → API → UI) and in deployment artifacts.

---

## 1. Current testing landscape

### 1.1 Inventory

| Layer | Location | Framework / runner | Cases | Used in CI? |
|---|---|---|---|---|
| C++ engine integration / golden | `CellEvoX/tests/test_simulation.cpp` | Catch2 v3.4.0 (FetchContent) | 28 `TEST_CASE` (many with multiple `SECTION`) | Yes (`~[benchmark]`) |
| C++ unit (population step) | `CellEvoX/tests/test_common_population_step.cpp` | Catch2 | 3 | Yes |
| C++ snapshot ABI contract | `CellEvoX/tests/test_population_snapshot_io.cpp` | Catch2 | 6 | Yes |
| C++ output/CSV contract | `CellEvoX/tests/test_run_data_engine.cpp` | Catch2 | 5 | Yes |
| C++ performance | `CellEvoX/tests/bench_simulation.cpp` | Catch2 benchmark + custom Python gate | 3 (`[benchmark]`) | Yes (paired base/head) |
| Python visualization regression | `CellEvoX/scripts/test_plot_muller_and_phylogeny.py` | `unittest`, run directly | 11 test methods | Yes (PR only) |
| Python cross-language snapshot reader | `CellEvoX/scripts/snapshot_io.py` | — | **none** | — |
| Python analysis scripts | `plot_*.py`, `visualize_tumor_3d.py`, `animate_clone_growth_2d.py`, `generate_test_data.py` | — | **none** | — |
| Web backend | `web/backend/` (FastAPI, runner, results parser, batch) | — | **none** | — |
| Web frontend | `web/frontend/` (React 19 / TS / Vite) | — | **none** (no test script, no Vitest/Playwright) | — |
| CLI / binary end-to-end | `CellEvoX/src/main.cpp` | — | **none** | — |
| Docker image | `Dockerfile` | — | **none** | Build only |
| PR change contract | `.github/scripts/validate_pr_contract.py` | custom Python | **none** (validator itself untested) | Yes (PR only) |

Tooling present but **not wired in**: `CellEvoX/tests/coverage_report.py` (gcov JSON), `catch_discover_tests()` in CMake (CTest registration), `.clang-format`, frontend ESLint/`tsc`.

`CellEvoX/scripts/test_radial.py` is **not a test** despite its name — it is a plotting CLI. This is a discoverability trap.

### 1.2 Structure and maintainability

**Strengths**

- No mocking framework, no mocks. The suite exercises real engine objects and real filesystem artifacts.
- Tests assert **observable behavior and public artifacts** (snapshot bytes, CSV headers/rows, termination metadata, plot files) rather than private methods — exactly the right bias for this domain.
- Determinism is treated as a first-class feature: seeded golden comparison, thread-count invariance (1 vs 4, 1 vs 5), and full-trajectory 2D/3D parity (`generational_stat_report`, every cell, and the whole graveyard).
- `correctness_baseline.json` is checked in and regenerated deliberately via `CELLEVOX_UPDATE_BASELINE=1`, which makes intentional change review possible.
- Snapshot tests cover v1 legacy read, truncated v2 rejection, unknown sizes, payload-kind header flags, and missing-file behavior — genuine contract tests.

**Weaknesses**

1. **Engine-level, not unit-level.** Only 3 focused unit cases exist (`test_common_population_step.cpp`). Everything else builds a `SimulationEngine`/`RunDataEngine` and inspects files. Failures therefore point at a large surface, and covering edge cases multiplicatively is expensive.
2. **Golden fragility.** Exact seeded numeric baselines can change with compiler, libstdc++, TBB, Eigen version, or `CELLEVOX_NATIVE_OPTIMIZATIONS`/`-march=native`. The baseline is generated on one machine and compared on GitHub runners; a toolchain bump produces a red build with no signal about *what* changed.
3. **`require_approx` treats `NaN == NaN`.** This silently accepts NaN-producing logic paths as "equal", which is risky for a numeric simulator.
4. **Shared `/tmp/cellevox_tests/...` paths** and `remove_all` between tests. Running shards in parallel on one runner (or reusing a self-hosted runner) can collide and create flaky, order-dependent failures.
5. **No per-test timeouts.** A deadlock in the threaded engine or a benchmark hang blocks the job until the runner-level default (6 h) expires.
6. **2D/3D parity covers the shared event model, but not spatial mechanics.** `test_simulation.cpp` compares full `generational_stat_report` values, every cell, and the whole graveyard — stronger than a single event payload. What is *not* compared: spatial spring relaxation, grid rebuild, and runs above capacity. The duplicated 2D loop (see `AGENTS.md`) still makes parity a test obligation.
7. **No explicit capacity test.** `env_capacity` is soft / never a ceiling, but no test *asserts* that. Coverage is indirect: the 3D determinism test sets `env_capacity = 0` with 64 cells (so `N > Nc` happens) and `test_common_population_step.cpp` exercises `N == Nc`. There is no 2D over-capacity case, no assertion that population is not clamped, and no statistics check at/over capacity.
8. **Coverage script exists but is dead.** `coverage_report.py` parses gcov JSON but is not referenced by CMake, CI, or docs. There is no coverage number for this repository at any point in its history.

### 1.3 Are we testing behavior or implementation details?

Mostly behavior — good. Exceptions:

- `test_simulation.cpp` asserts exact CSV row values, which is appropriate for a public contract but means unrelated formatting changes break tests.
- The benchmark harness (`bench_simulation.cpp`) constructs its own `std::mt19937` even though the common step's RNG parameter is documented as dead. Benchmarks therefore measure a path that differs from the production RNG design; this is a measurement-validity risk, not a test failure.

### 1.4 Coverage tracking

- No coverage job in CI.
- `coverage_report.py` supports `--json-out`, filtering of `include/external`, exclusion of `main.cpp`, overall line coverage and least-covered files — a ready-made foundation.
- No Python coverage (`coverage.py`) and no frontend coverage tooling at all.

**Conclusion:** coverage is effectively **unmeasured**. For a repo with 3 500+ LOC of non-external C++ and ~4 800 LOC of Python/web code, this is the single largest blind spot.

---

## 2. CI/CD and quality gates

### 2.1 Workflow inventory

| Workflow | Triggers | Jobs | Gate strength |
|---|---|---|---|
| `.github/workflows/ci.yml` | `push: main`, `pull_request: main`, `workflow_dispatch` | build base @ base SHA, build head, unit tests, paired benchmark, upload report | **Strong** for perf; weak for breadth |
| `.github/workflows/quality-gates.yml` | PR (opened/edited/synchronize/reopened/ready), manual | PR contract validator; Python viz regression test | Good idea, narrow coverage |
| `.github/workflows/docker-publish.yml` | daily cron, push `main`/tags, PR `main` | Buildx build, push non-PR, cosign sign | **No relationship to tests** |

### 2.2 What runs on PR vs main

| Check | PR to `main` | Push to `main` | Tag / deploy |
|---|---|---|---|
| C++ build (warnings-as-errors) | Yes | Yes | — |
| C++ unit + integration tests | Yes | Yes | — |
| Paired performance gate | Yes (`quick` only) | Yes (`quick` only) | — |
| PR change contract | Yes (non-draft) | **No** | — |
| Python visualization test | Yes (including drafts) | **No** | — |
| Coverage | **No** | **No** | — |
| Sanitizers / TSan | **No** | **No** | — |
| clang-tidy / clang-format | **No** | **No** | — |
| Frontend `eslint` / `tsc` | **No** | **No** | — |
| Python lint/type-check | **No** | **No** | — |
| Web backend tests | **No** | **No** | — |
| E2E binary smoke test | **No** | **No** | — |
| SAST (CodeQL) | **No** | **No** | — |
| Dependency audit (`pip-audit`, `npm audit`, Dependabot) | **No** | **No** | — |
| Secret scanning | **No** | **No** | — |
| Docker build | Yes | Yes | Yes (push + sign) |

Two structural inconsistencies stand out:

- The **contract gate runs only on PRs**. A direct push to `main` (or a merge whose metadata is lost) bypasses it entirely.
- The **visualization test runs on draft PRs** while the contract does not — wasted compute on incomplete work, and inconsistent semantics for "draft".

### 2.3 Static analysis evaluation

| Tool | Configured? | Enforced? |
|---|---|---|
| Compiler warnings | `-DCELLEVOX_WARNINGS_AS_ERRORS=ON` for **head only** | Yes (head), base intentionally permissive |
| clang-format | `.clang-format` (Google, 2-space, 100 col) | **No check anywhere** |
| clang-tidy | Not configured | No |
| ESLint | `web/frontend/eslint.config.js`, `npm run lint` | **Never invoked in CI** |
| TypeScript | `tsc -b` inside `npm run build` | **Never invoked in CI** |
| Python linter/formatter | None (no ruff/flake8/black/mypy) | No |
| SAST | None | No |
| Dependency scanning | None | No |
| Pre-commit hooks | No `.pre-commit-config.yaml`; no `hooks/` | No |
| Secret scanning | `.gitignore` covers `*.gpg/*.asc/*.pub` but **not** `~/.openrouter_key`/`~/.gemini_key` (they live outside the repo, so acceptable) | n/a |

### 2.4 Bottlenecks and caching

- **ccache is present** for C++ via `hendrikmuhs/ccache-action`; cache key includes `github.ref_name` and falls back to `main`. Reasonable.
- **No caching for apt** (`libtbb`, `libeigen3`, `nlohmann-json`, `python3-*`), **no pip cache**, **no npm cache**, and **no FetchContent cache** for Catch2. Every run re-downloads Catch2 and re-installs system packages.
- **Base is rebuilt from source on every run.** That is required for a fair paired benchmark, but the base build reuses the same ccache — good. A nightly "base binary" artifact keyed by base SHA would cut most of this cost.
- **No `concurrency` group in `ci.yml`**, so a rapid PR push queues full benchmark runs that will be superseded.
- **No `timeout-minutes`** anywhere in `ci.yml` or `docker-publish.yml`. A hang (threaded engine deadlock, benchmark retry loop) consumes 6 h of runner time.
- **No `permissions:` block in `ci.yml`/`docker-publish.yml`** → default token permissions are broader than necessary. `quality-gates.yml` correctly sets `contents: read`.
- **Action pinning is inconsistent.** `docker-publish.yml` pins third-party actions by SHA; `ci.yml` and `quality-gates.yml` use mutable tags (`actions/checkout@v4`, `actions/setup-python@v5`, `hendrikmuhs/ccache-action@v1.2`). Supply-chain posture differs by workflow.
- **No path filters**: documentation-only PRs still run the full benchmark gate.
- **Benchmark default is `quick` only.** `timing` and `snapshot` groups exist but never run automatically. The 2M-cell snapshot serialization path and 2D-vs-3D timing are therefore unguarded on every merge.

### 2.5 Steps where failures can be silently ignored

1. `quality-gates.yml` — `git fetch ... || true`. If the base commit is not fetchable, the validator runs against a missing/mismatched base and may mis-evaluate scope instead of failing. The committed workflow fails loudly instead, and is pull-request only, because a dispatched run has no PR body to validate.
2. `quality-gates.yml` — contract **warnings are non-fatal by design**; only errors block. This is documented, but means "low risk touching core paths" can merge with no recorded mitigation.
3. `ci_benchmark_gate.py` — **new benchmarks do not fail** the gate (only regressed/missing do). A PR that renames a benchmark and adds an unmeasured replacement can pass.
4. `ci.yml` — base build still installs Boost and passes `-DSKIP_GUI=ON`; `docs/runbook.md` explicitly calls these dead leftovers from an older baseline. The base build is therefore not exactly "the current build minus the diff"; it silently inherits obsolete options.
5. `ci.yml` — **unit tests are invoked directly** (`./head_build/bin/CellEvoXTests "~[benchmark]"`) rather than through `ctest`. `catch_discover_tests()` is registered but unused, so per-test reporting, timeouts, and parallel scheduling are lost.
6. `docker-publish.yml` — publishes/signs `main` and tags **without waiting for or depending on CI**. A red `main` can still produce a signed image.
7. No artifact retention of test logs/JUnit XML; only benchmark JSON is uploaded.

### 2.6 Docker / deployment pipeline

- `Dockerfile` is a **development environment**: it installs Ubuntu packages, Node 22, Python deps, ffmpeg, ccache, then `COPY . /home/${USER}/CellEvoX`. There is **no CMake build, no frontend build, no `CMD`, no healthcheck**.
- README claims the image "builds for the target host", but the image contains no built simulator.
- `docker-publish.yml` comments claim multi-platform, but **no `platforms:` key exists** — only the runner's native architecture is built.
- No container smoke test (`docker run ... --help`, start API, hit `/health`).

---

## 3. Gap analysis and regression risks

### 3.1 Highest-risk untested areas

| # | Risk area | Why it matters | Current coverage | Risk |
|---|---|---|---|---|
| 1 | **C++ → Python snapshot ABI** (`CellEvoX/scripts/snapshot_io.py`) | Binary layout is a public contract; struct sizes, flags, and driver-payload semantics are duplicated in a second language. A C++ change can pass all C++ tests while silently breaking every Python plot and the web UI. | C++ side tested; Python reader **zero** tests | **Critical** |
| 2 | **Web backend orchestration** (`web/backend/runner.py`) | 763 LOC of subprocess lifecycle, atomic config writes, SIGINT, WebSocket queues, batch accounting. Only one global job. No tests. | None | **Critical** |
| 3 | **Results parsing / API surface** (`results_parser.py`, `main.py`) | Reads files produced by C++; drift is invisible until a user sees an empty chart or a 500. `_find_run_dirs` scans the whole repo; run-ID normalization can collide. | None | **High** |
| 4 | **Capacity invariant (`N > Nc`)** | Documented hard invariant: capacity is soft, never a ceiling. No test asserts no clamping, no crash, or correct statistics at/over capacity. | Indirect only (3D `env_capacity = 0` with 64 cells; `N == Nc` unit case); no 2D variant, no explicit assertion | **High** |
| 5 | **2D vs 3D parity beyond the event model** | Duplicated 2D loop (known tech debt, #49). Full trajectories, statistics, cells, and graveyard are compared; spatial mechanics and over-capacity runs are not. | Partial | **Medium** |
| 6 | **Frontend** | 3 200+ LOC React/TS; config payload construction, batch queue, run polling, error boundaries. `tsc`/ESLint never run in CI; no component or E2E tests. | None | **High** |
| 7 | **CLI / end-to-end run** | `main.cpp` argument parsing, `--postprocess` modes, output naming, exit codes. Nothing invokes the binary with a real config in CI. | None | **High** |
| 8 | **Config validation breadth** | Some unsafe values rejected, but no schema test for unknown keys, negative values, extreme mutation probabilities, resolution semantics. Web accepts arbitrary `dict`. | Partial | **Medium** |
| 9 | **Analysis path constants** | Hardcoded `0.005` tau in the analysis path, `--postprocess csv` inconsistency, dead `log_level`. Untested paths can drift from engine output. | None | **Medium** |
| 10 | **Build portability / UB** | Only one Release build, native optimizations ON, no ASan/UBSan/TSan. UB in threaded code can survive tests and appear only in production. | None | **Medium** |
| 11 | **Docker artifact** | Published signed image is not the application; no build smoke test. | None | **Medium** |
| 12 | **PR contract validator** | 10 KB of parsing/diff logic; a bug can either block valid PRs or pass invalid ones. | None | **Medium** |

### 3.2 Missing testing layers

- **Unit layer** for `CommonPopulationStep`, RNG streams, `SpatialHashGrid` edge cases, mutation classification.
- **Contract layer** on the Python/JS side of the snapshot + CSV contracts (only C++ is asserted).
- **API contract layer** (OpenAPI schema snapshot + FastAPI `TestClient` tests).
- **Database integration** — not applicable: no database exists. Instead the equivalent is **filesystem/run-directory integration**, which is only partially covered.
- **Critical-path E2E**: build binary → run a tiny config → assert run directory, CSV headers, snapshot header, exit code, and postprocess outputs.
- **Frontend E2E**: start backend with a fake run directory, load Results page, assert chart/table rendering.
- **Security/dependency layer**: no SAST, no `pip-audit`, no `npm audit`, no Dependabot.
- **Performance breadth**: `timing` and `snapshot` groups never run automatically.

### 3.3 Architectural patterns that make testing hard

1. **Duplicated 2D event loop** — the same semantics exist in two implementations, so parity is a test obligation rather than a compiler guarantee.
2. **Counter-based RNG keyed by `(seed, step, cell_id, stream)`** — excellent for determinism, but edge cases require carefully constructed scenarios; ad-hoc `std::mt19937` harnesses measure a different path.
3. **Public binary ABI spread across three languages** (C++, Python, TypeScript types) with no shared schema or generated codec.
4. **Global single-job web runner** — process-wide mutable state makes unit testing and parallel test execution difficult; a `Runner` instance with injectable binary path, clock, and process factory would be testable.
5. **Disk-coupled modules** — plotting functions take a run directory and read files directly; no in-memory seam. Tests must build fixture trees on disk (done well in the viz test, but not reusable elsewhere).
6. **Monolithic `RunDataEngine.cpp`** (1 379 LOC) mixing statistics, CSV output, snapshots, phylogeny, and analysis; hard to unit-test pieces.
7. **Embedded Python via `matplotlibcpp.h`** — a large external header compiled into the engine; errors surface only at runtime.
8. **CMake `FetchContent` for Catch2** — test infrastructure depends on network availability; no vendored/offline path or hash pin observed.
9. **Native CPU tuning defaults ON** — good locally, but it makes golden numeric baselines and performance comparisons environment-sensitive.

---

## 4. Actionable roadmap

### 4.1 Quick wins (low effort, immediate regression protection)

**QW-1 — Wire the existing C++ tests through CTest and add coverage.**

```yaml
# .github/workflows/ci.yml (head build step)
- name: Build Head (Current PR)
  run: |
    cmake -B head_build -S head/CellEvoX \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
      -DCELLEVOX_WARNINGS_AS_ERRORS=ON
    cmake --build head_build --target CellEvoX CellEvoXTests -j$(nproc)

- name: Run C++ tests (CTest, JUnit output, per-test timeout)
  working-directory: head_build
  run: ctest --output-on-failure --timeout 120 \
       --output-junit "$RUNNER_TEMP/ctest.xml" -LE benchmark

- name: Upload test results
  if: always()
  uses: actions/upload-artifact@v4
  with:
    name: ctest-results
    path: ${{ runner.temp }}/ctest.xml
```

Coverage job (uses the already-written `coverage_report.py`):

```yaml
  coverage:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y cmake g++ libtbb-dev libeigen3-dev libspdlog-dev nlohmann-json3-dev python3-dev
      - name: Configure with coverage
        run: cmake -B cov_build -S CellEvoX -DCMAKE_BUILD_TYPE=Debug -DCELLEVOX_ENABLE_COVERAGE=ON
      - name: Build
        run: cmake --build cov_build --target CellEvoXTests -j$(nproc)
      - name: Run
        run: ./cov_build/bin/CellEvoXTests "~[benchmark]"
      - name: Report
        run: python3 CellEvoX/tests/coverage_report.py --build-dir cov_build --json-out coverage.json | tee coverage.txt
```

Then ratchet a threshold in a tiny gate (start at today's measured value, raise by 2–5 points per milestone) rather than blocking immediately at an arbitrary number.

**QW-2 — Add a Python contract test for the snapshot reader (highest ROI in the repo).**

Generate one small run with the real binary, then assert that `snapshot_io.py` decodes it identically to the C++ reader. Reuse the same fixture across C++ and Python so the ABI is pinned once.

```python
# CellEvoX/scripts/test_snapshot_io_contract.py
import struct, subprocess, sys, tempfile
from pathlib import Path
import snapshot_io

HEADER = struct.Struct("<8sIIdIIBBB13x")

def test_binary_header_matches_cpp_contract(tmp_path):
    cfg = tmp_path / "config.json"
    cfg.write_text('''{
      "simulation_mode": "stochastic",
      "initial_cells": 4,
      "env_capacity": 100,
      "max_time": 1.0,
      "seed": 1234,
      "statistics_resolution": 1,
      "population_statistics_res": 1,
      "output_path": "%s",
      "snapshot_interval": 1
    }''' % tmp_path)
    subprocess.run([sys.argv[1], str(cfg)], check=True)  # pass binary path via argv
    bins = sorted((tmp_path / "population_data").glob("*.bin"))
    assert bins, "run produced no binary snapshot"
    raw = bins[-1].read_bytes()
    magic, version, record_size, tau, count, drv, dims, mut_size, flags = HEADER.unpack_from(raw)
    assert magic == b"CELXPOP1" and version == 2
    assert (record_size, mut_size) == (36, 5)
    frame = snapshot_io.load_population_frames(tmp_path)[-1]
    assert len(frame.data) == count
```

**QW-3 — Make the silent-failure paths loud.**

- Replace `git fetch ... || true` with a retry-then-fail step.
- Add `if: always()` artifacts for contract output.
- Fail the benchmark gate when a configured benchmark is **missing** (already does) and when all benchmarks in a group are `inconclusive`.
- Add `timeout-minutes` to every job (`ci.yml`: 30 build/test, 60 benchmark).
- Add `permissions: contents: read` to `ci.yml` and `docker-publish.yml`.

**QW-4 — Add concurrency, path filters, and dependency caching.**

```yaml
concurrency:
  group: ci-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: true

on:
  pull_request:
    paths-ignore: ['docs/**', '**/*.md', 'assets/**']

- uses: actions/cache@v4
  with:
    path: ~/.cache/pip
    key: pip-${{ hashFiles('**/requirements*.txt', 'web/backend/requirements.txt') }}

- uses: actions/cache@v4
  with:
    path: ~/.npm
    key: npm-${{ hashFiles('web/frontend/package-lock.json') }}

- uses: actions/cache@v4
  with:
    path: head_build/_deps
    key: deps-${{ hashFiles('CellEvoX/CMakeLists.txt') }}
```

**QW-5 — Run frontend static checks and backend import smoke.**

```yaml
  web-static:
    runs-on: ubuntu-24.04
    defaults: { run: { working-directory: web/frontend } }
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with: { node-version: 22, cache: npm, cache-dependency-path: web/frontend/package-lock.json }
      - run: npm ci
      - run: npm run lint
      - run: npm run build
```

```yaml
  backend-smoke:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: '3.12', cache: pip }
      - run: pip install -r web/backend/requirements.txt
      - run: python -c "import main, runner, results_parser"  # working-directory: web/backend
```

**QW-6 — Add pre-commit hooks** (catches formatting/lint locally before CI).

```yaml
# .pre-commit-config.yaml
repos:
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v4.6.0
    hooks:
      - id: trailing-whitespace
      - id: end-of-file-fixer
      - id: check-yaml
      - id: check-json
      - id: check-added-large-files
  - repo: https://github.com/pre-commit/mirrors-clang-format
    rev: v18.1.8
    hooks:
      - id: clang-format
        files: ^CellEvoX/(src|include|tests)/.*\.(cpp|hpp|h)$
  - repo: https://github.com/astral-sh/ruff-pre-commit
    rev: v0.6.9
    hooks:
      - id: ruff
        args: [--fix]
        files: ^(web|CellEvoX/scripts)/.*\.py$
```

**QW-7 — Fix known contract drift now** (each is a one-line change with a test):

- Remove `"deterministic"` from `get_config_schema()` in `web/backend/main.py` (violates `AGENTS.md`).
- Accept `csv` in `_validate_web_batch_outputs` or remove it from `runner._valid_postprocess_modes` — pick one documented contract.
- Replace `main.py`'s `config: dict` with a validated Pydantic model (at minimum `extra="forbid"`).
- Add `web/frontend/dist` to root `.gitignore` (currently only the frontend-local ignore covers it).

**QW-8 — Remove dead CI artifacts**: drop `libboost-program-options-dev` and `-DSKIP_GUI=ON` from the base build only after verifying the oldest supported base still configures. Better: pin the comparison base to a known-good commit range or store a base binary artifact.

### 4.2 Medium-term goals (fill key gaps, harden gates)

**MT-1 — Coverage ratchet + per-file thresholds for critical files.**
Set a repository threshold and a higher threshold for `src/systems/*`, `src/core/RunDataEngine.cpp`, and `include/systems/CommonPopulationStep.hpp`. Failing the build on a drop prevents silent erosion.

**MT-2 — Add the missing unit/contract tests, in priority order:**

1. `CommonPopulationStep`: birth/death/mutation boundaries, zero-probability, capacity-at-limit, deterministic event ordering, RNG stream independence.
2. Capacity invariant: `N > Nc` (2D and 3D) — no clamp, no crash, statistics remain finite; assert explicitly that population exceeds capacity.
3. 2D/3D parity harness: extend the existing full-trajectory comparison to spatial mechanics (spring relaxation, grid rebuild) and to `N > Nc` runs, then fold it into the shared loop (#49).
4. RNG determinism matrix: identical outputs for thread counts 1/2/4/8 and for repeated runs; different seeds differ.
5. Snapshot golden bytes: store a tiny canonical snapshot in `tests/fixtures/` and assert exact header/record bytes across C++ and Python.
6. Analysis-path tau: test that the hardcoded `0.005` path or its replacement is exercised with non-default tau.
7. CLI: `--postprocess full|exports|csv`, missing config, invalid mode, exit codes, output directory naming.

**MT-3 — Backend API + runner tests with real seams.**

```python
# web/backend/tests/test_api.py
from fastapi.testclient import TestClient
import main

client = TestClient(main.app)

def test_config_schema_has_no_dead_modes():
    schema = client.get("/api/config/schema").json()
    assert "deterministic" not in schema["simulation_mode"]["values"]

def test_start_rejects_invalid_postprocess():
    r = client.post("/api/simulations", json={
        "config": {"simulation_mode": "stochastic", "max_time": 1},
        "postprocess": "not-a-mode",
    })
    assert r.status_code == 422
```

Runner changes to enable testing (not test-only hacks — genuine improvements):

- Extract a `Runner` class with injected `binary_path`, `clock`, and process factory.
- Replace the global job with a `JobRegistry` (or keep a single-job policy but enforce it with a lock and a test).
- Fix `self._batch_completed += 1` placement so completed counts exclude failures (or expose separate succeeded/failed counters).

**MT-4 — Sanitizer matrix (nightly + required on changes to `src/systems`, `src/ecs`, `include/utils`).**

```yaml
  sanitizers:
    runs-on: ubuntu-24.04
    strategy:
      matrix:
        sanitizer: [address, undefined, thread]
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y cmake g++ libtbb-dev libeigen3-dev libspdlog-dev nlohmann-json3-dev python3-dev
      - run: |
          cmake -B san -S CellEvoX -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DCELLEVOX_NATIVE_OPTIMIZATIONS=OFF \
            -DCMAKE_CXX_FLAGS="-fsanitize=${{ matrix.sanitizer }} -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=${{ matrix.sanitizer }}"
      - run: cmake --build san --target CellEvoXTests -j$(nproc)
      - run: ./san/bin/CellEvoXTests "~[benchmark]" 
        env:
          ASAN_OPTIONS: detect_leaks=1
          UBSAN_OPTIONS: print_stacktrace=1
```

Note: run ASan/UBSan and TSan as separate jobs (they are mutually incompatible in one binary).

**MT-5 — Static analysis and supply chain.**

- CodeQL for C++ and Python on PR + weekly schedule.
- `pip-audit -r web/backend/requirements.txt` and `npm audit --audit-level=high`.
- Dependabot for `github-actions`, `pip`, `npm`, and `docker`.
- Pin all actions by commit SHA in `ci.yml` and `quality-gates.yml` (match `docker-publish.yml`).
- Enable branch protection: require `bench-regression-check`, `contract`, `python-viz-regressions`, `web-static`, `backend-smoke`, and `coverage` before merge; disallow direct pushes to `main`.

**MT-6 — E2E binary smoke test (fast, high value).**

```yaml
  e2e-smoke:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y cmake g++ libtbb-dev libeigen3-dev libspdlog-dev nlohmann-json3-dev python3-dev
      - run: cmake -B build -S CellEvoX -DCMAKE_BUILD_TYPE=Release && cmake --build build --target CellEvoX -j$(nproc)
      - run: ./build/bin/CellEvoX quick_config.json
      - run: |
          test -f output/*/statistics/generational_statistics.csv
          test -f output/*/population_data/*.bin
          test -f output/*/run_metadata.json
          head -c 8 output/*/population_data/*.bin | grep -q CELXPOP1
```

**MT-7 — Run all benchmark groups on a schedule.**

```yaml
on:
  schedule:
    - cron: '0 3 * * *'   # nightly full perf run
# ...
env:
  BENCHMARK_GROUPS: ${{ github.event_name == 'schedule' && 'quick,timing,snapshot' || 'quick' }}
```

Persist `perf-gate.json` over time (branch/artifact or a small JSON store) to detect slow drift that stays under the 5% per-PR threshold.

**MT-8 — Make golden baselines toolchain-aware.** Record compiler/lib versions with the baseline; regenerate only in a dedicated PR; compare with a tolerance for floating-point fields. Avoid `-march=native` in the CI golden run.

### 4.3 Long-term strategy

1. **Single source of truth for contracts.** Define the snapshot and CSV schemas in one machine-readable file (JSON Schema / FlatBuffers / generated codec). Generate the C++ writer/reader, Python reader, and TypeScript types from it, or at minimum drive cross-language tests from one fixture corpus. This eliminates the highest-severity risk class in the repo.
2. **Unify the 2D and 3D event loops.** A single templated step parameterized by spatial policy removes the parity invariant as a test obligation and shrinks the surface that needs E2E coverage.
3. **Introduce a simulation record/replay corpus.** Store a handful of tiny deterministic runs (config + expected CSV/snapshot hashes) as fixtures. Any toolchain or refactor regression is caught by replay across C++ and Python.
4. **Testable runner architecture.** Replace the global single-job runner with an explicit job registry and dependency-injected process/clock; add frontend E2E (Playwright) against a fake backend.
5. **Deployment integrity.** Make Docker a real artifact: multi-stage build (compile simulator + build frontend + runtime image), healthcheck, non-root user, and a `docker run` smoke test gating publish. Require CI success before publishing (`needs:` / `workflow_run`).
6. **Mutation testing (C++) on critical modules** (`CommonPopulationStep`, snapshot IO, RNG) to validate that tests actually detect behavior changes — the strongest available answer to "are we testing behavior?".
7. **Hermetic builds.** Vendor or pin FetchContent dependencies by hash; provide an offline build path; produce reproducible Release binaries for benchmark comparability.
8. **Quality dashboard.** Publish coverage, benchmark trend, and test-duration metrics per merge to make regressions visible without reading logs.

---

## 5. Suggested sequencing

| Phase | Actions | Outcome |
|---|---|---|
| Week 1 | QW-3, QW-4, QW-5, QW-7, QW-8 | Loud failures, caching/concurrency, web static checks, drift fixed |
| Week 2 | QW-1, QW-2, QW-6 | `ctest` + coverage reporting, Python snapshot contract test, local hooks |
| Weeks 3–5 | MT-1, MT-2 (items 1–4), MT-6 | Coverage ratchet, core unit/parity/capacity tests, E2E smoke |
| Weeks 6–8 | MT-3, MT-4, MT-5, MT-7 | Backend tests, sanitizers, SAST/dependency scanning, nightly perf |
| Quarter | 4.3 items 1–5 | Contract single-source, unified loops, replay corpus, real container artifact |

---

## 6. Bottom line

- **Keep:** the deterministic golden approach, the paired base/head performance gate, the PR change contract, and the no-mocks philosophy. These are genuine strengths.
- **Fix first:** the undetected cross-language contract boundary (Python snapshot reader + web parser), capacity/parity unit coverage, and the CI's silent-failure and no-coverage gaps.
- **Biggest single risk:** a change to the C++ snapshot/CSV contract or the web runner can merge green and break every downstream consumer, because all automated verification stops at the C++ process boundary.
