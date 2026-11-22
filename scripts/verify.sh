#!/usr/bin/env bash
#
# Single local verification entrypoint (issue #54).
#
# Every stage invokes the same command the matching GitHub Actions job runs,
# with the same flags, filters, and working directory, so a green local run
# means the same checks passed as on a pull request. CI calls these stages
# where it can (see .github/workflows/ci.yml), so this file is the single
# source of truth for what "done" means locally.
#
# Fail fast: the first failing stage stops the script, every invoked command is
# printed, and no step is best-effort (no `|| true`).

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT_DIR"

BUILD_DIR="${BUILD_DIR:-build/verify}"
ALL_STAGES=(gate-tests cpp-build cpp-test bench viz snapshot frontend backend review)

MODE="quick"
STAGES=()

usage() {
  cat <<'EOF'
Usage: scripts/verify.sh [--quick | --full] [--stage STAGE]...

  --quick        (default) configure + build CellEvoXTests, then run CTest.
  --full         Every stage below, in order.
  --stage NAME   Run only NAME (repeatable; overrides the mode defaults).

Stages:
  gate-tests     stdlib unit tests for the instruction-gate verdict logic
  cpp-build      cmake configure + build CellEvoX and CellEvoXTests
                 (Release, warnings as errors)
  cpp-test       ctest --output-on-failure (~[benchmark] Catch2 cases)
  bench          the CI "quick" benchmark group, head binary only
  viz            Python visualization regression test
  snapshot       Python snapshot ABI contract test
  frontend       npm ci + npm run lint + npm run build
  backend        web/backend pytest suite
  review         review-tooling pytest suite

The instruction-count gate itself needs a base tree and is not a local stage;
docs/runbook.md "CI notes" shows how to reproduce it.

Required toolchain: bash, cmake, ctest, a C++23 compiler, Python 3 with
development headers and NumPy, and the native dependencies listed in
docs/runbook.md. Optional per stage: ccache (used when present), Node.js + npm
(frontend), pytest + FastAPI deps (backend/review), matplotlib/pandas/networkx/
pyfish (viz), pandas (snapshot). A stage whose dependencies are missing fails
immediately with the install command instead of skipping.

Environment:
  JOBS           parallelism for the C++ build (default: detected core count).
  BUILD_DIR      CMake build directory (default: build/verify). CI points this
                 at its own build tree.
  CTEST_JUNIT    when set, also write a JUnit report to this path (as CI does).
EOF
}

die() {
  printf 'verify.sh: error: %s\n' "$*" >&2
  exit 1
}

run() {
  printf '\n+ '
  printf '%q ' "$@"
  printf '\n'
  "$@"
}

run_in() {
  local dir="$1"
  shift
  printf '\n+ (cd %q && ' "$dir"
  printf '%q ' "$@"
  printf ')\n'
  ( cd "$dir" && "$@" )
}

require() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1${2:+ — $2}"
}

python_has() {
  python3 -c "import $1" >/dev/null 2>&1
}

while (($#)); do
  case "$1" in
    --quick)
      MODE="quick"
      ;;
    --full)
      MODE="full"
      ;;
    --stage)
      (($# >= 2)) || die "--stage requires a value"
      STAGES+=("$2")
      shift
      ;;
    --stage=*)
      STAGES+=("${1#*=}")
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1 (see --help)"
      ;;
  esac
  shift
done

if ((${#STAGES[@]} == 0)); then
  if [[ "$MODE" == "quick" ]]; then
    STAGES=(cpp-build cpp-test)
  else
    STAGES=("${ALL_STAGES[@]}")
  fi
fi

for stage in "${STAGES[@]}"; do
  known=0
  for candidate in "${ALL_STAGES[@]}"; do
    if [[ "$stage" == "$candidate" ]]; then
      known=1
      break
    fi
  done
  ((known)) || die "unknown stage: $stage (valid: ${ALL_STAGES[*]})"
done

if [[ -z "${JOBS:-}" ]]; then
  JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
fi

stage_gate_tests() {
  require python3
  # CI runs this before installing dependencies: the gate modules must stay
  # stdlib-only.
  run python3 CellEvoX/tests/benchmarks/test_perf_ir_gate.py
}

stage_cpp_build() {
  require cmake
  # Same configure flags as CI's head build. CI additionally builds
  # CellEvoXPerfProbe for the paired instruction gate, which needs a base tree
  # and has no local stage (see docs/runbook.md "CI notes").
  local cmake_args=(
    -B "$BUILD_DIR"
    -S CellEvoX
    -DCMAKE_BUILD_TYPE=Release
    -DCELLEVOX_WARNINGS_AS_ERRORS=ON
  )
  if command -v ccache >/dev/null 2>&1; then
    cmake_args+=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  fi
  run cmake "${cmake_args[@]}"
  run cmake --build "$BUILD_DIR" --target CellEvoX CellEvoXTests --parallel "$JOBS"
}

stage_cpp_test() {
  require ctest
  [[ -x "$BUILD_DIR/bin/CellEvoXTests" ]] ||
    die "no test binary at $BUILD_DIR/bin/CellEvoXTests; run --stage cpp-build first"
  local ctest_args=(--output-on-failure)
  if [[ -n "${CTEST_JUNIT:-}" ]]; then
    ctest_args+=(--output-junit "$CTEST_JUNIT")
  fi
  run_in "$BUILD_DIR" ctest "${ctest_args[@]}"
}

stage_bench() {
  local binary="$BUILD_DIR/bin/CellEvoXTests"
  [[ -x "$binary" ]] || die "no test binary at $binary; run --stage cpp-build first"
  # Same test case and benchmark options the CI gate uses for the "quick"
  # group (CellEvoX/tests/benchmarks/ci_benchmark_gate.py GROUP_FILTERS).
  run "$binary" "Simulation: High-Performance Regression" \
    --benchmark-samples 3 \
    --benchmark-warmup-time 200 \
    --benchmark-resamples 20000 \
    --benchmark-confidence-interval 0.95
}

stage_viz() {
  require python3
  local missing=()
  for module in matplotlib pandas networkx pyfish; do
    python_has "$module" || missing+=("$module")
  done
  ((${#missing[@]} == 0)) ||
    die "visualization stage is missing Python modules: ${missing[*]} — install them with: python3 -m pip install -r CellEvoX/scripts/requirements-visualization.txt"
  run env MPLBACKEND=Agg python3 CellEvoX/scripts/test_plot_muller_and_phylogeny.py
}

stage_snapshot() {
  require python3
  python_has pandas ||
    die "snapshot stage needs pandas: python3 -m pip install pandas"
  run python3 CellEvoX/scripts/test_snapshot_io.py -v
}

stage_frontend() {
  require npm "install Node.js 22 for the frontend"
  [[ -d web/frontend ]] || die "web/frontend is missing"
  run_in web/frontend npm ci
  run_in web/frontend npm run lint
  run_in web/frontend npm run build
}

stage_backend() {
  require python3
  python3 -c "import pytest, fastapi, httpx" >/dev/null 2>&1 ||
    die "backend stage needs the backend dependencies: python3 -m pip install -r web/backend/requirements.txt -r web/backend/requirements-dev.txt"
  run_in web/backend python3 -m pytest -q tests
}

stage_review() {
  require python3
  python_has pytest ||
    die "review stage needs pytest: python3 -m pip install pytest"
  run python3 -m pytest -q tools/test_ai_review.py
}

printf 'verify.sh: mode=%s stages=%s build=%s\n' "$MODE" "${STAGES[*]}" "$BUILD_DIR"

for stage in "${STAGES[@]}"; do
  printf '\n=== %s ===\n' "$stage"
  "stage_${stage//-/_}"
done

printf '\nverify.sh: OK (%s)\n' "${STAGES[*]}"
