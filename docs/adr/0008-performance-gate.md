# ADR-0008: Paired benchmark gate and phase profiling
Status: Accepted · 2026-10-24

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
- Qt/Boost and `-DSKIP_GUI=ON` in CI belong to an older baseline comparison and
  are candidates for removal once that baseline is updated.
