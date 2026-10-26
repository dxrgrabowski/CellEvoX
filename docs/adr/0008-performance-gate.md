# ADR-0008: Paired benchmark gate and phase profiling
Status: Accepted · 2026-10-26

## Context
Performance is dominated by memory layout and parallel phases. Absolute numbers
are noisy across machines, and unmeasured optimizations routinely move cost
elsewhere.
## Decision
Judge performance changes by paired base/head runs of the same benchmarks on the
same host, with 5% time and 10% memory thresholds. Phase timing is opt-in
(`CELLEVOX_PROFILE_PHASES`) and semantic-neutral. Amdahl's law decides whether
parallelizing a phase can matter.
## Consequences
- "Looks faster" is not evidence; record benchmark names and direction.
- Change one bottleneck at a time, then re-run correctness and the same benchmark.
- Thresholds and tuning constants are engineering defaults, not domain
  invariants, and may be revised with evidence.
- `-DSKIP_GUI=ON` in CI belongs to an older baseline comparison and is a
  candidate for removal once that baseline is updated. The Boost install has
  been removed.

## Evidence
- Paired gate: `CellEvoX/tests/benchmarks/ci_benchmark_gate.py` compares base and
  head benchmark JSON on the same host, with defaults `--threshold 0.05` (time)
  and `--memory-threshold 0.10` (memory), plus `--max-relative-ci 0.05`.
- Interactive comparison: `CellEvoX/tests/benchmarks/compare_benchmarks.py`
  exposes the same knobs; its standalone default threshold is looser (0.30) and
  is meant for exploratory runs, not for the CI gate.
- The measurement schedule (blocked vs interleaved rounds) is revised by
  ADR-0009; the thresholds above are unchanged.
- Benchmarks live in `CellEvoX/tests/bench_simulation.cpp`;
  `CELLEVOX_PROFILE_PHASES` drives the opt-in phase timing.
