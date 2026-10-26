# ADR-0009: Interleaved benchmark rounds with median aggregation
Status: Proposed · 2026-10-26

## Context
ADR-0008 measures base and head as two contiguous blocks on the same host. On a
shared GitHub-hosted runner that assumption fails: three consecutive runs of an
unchanged-code PR failed the gate only, with different benchmarks each time.
Those runs also show the machine itself moving — one attempt measured every
benchmark roughly 2.2x slower than the next. Whatever drifts during a block
(base first, head second) is attributed to the change.

Host isolation is not available to us: the runner is a VM, so `isolcpus`,
`nohz_full`, `SCHED_FIFO` or a real-time kernel are not reachable, and the
C++23/TBB toolchain keeps us on Linux anyway. The measurement schedule is the
lever we actually control.

## Decision
Each filter runs N interleaved rounds of `(base, head)` and flips the order every
round, so any slow patch of the host hits both binaries instead of only the
second one. Per benchmark, rounds are collapsed into the **median of the round
means** as the point estimate, with a bootstrap interval over rounds as the
uncertainty. The reported interval is never narrower than the average
within-round Catch2 precision divided by `sqrt(rounds)`.

Defaults move from "30 samples, then 60" to "7 rounds x 5 samples, retry at 10
per round", which keeps the total measured work per binary within ~20% of the
old schedule. `--rounds 1` reproduces the blocked schedule and is only for local
investigation.

## Alternatives considered
- **Moving benchmark output to tmpfs.** Rejected after measuring: snapshot writes
  are gated on `current_tau % popul_res`, and the realistic 2D benchmark
  (`popul_res = 2`, `tau_step = 0.005`, 450 steps) writes one snapshot and one
  statistics file per sample — 80 KB, about 5 MB per gate. There is no writeback
  pressure to remove.
- **CPU time instead of wall clock** (`CLOCK_PROCESS_CPUTIME_ID`, insensitive to
  steal and descheduling). Rejected for now: what remains after interleaving is
  TBB scheduling variance, and process CPU time sums over worker threads, so it
  is equally sensitive to how the pool is scheduled; it would also change what
  the gate measures (work invested, not time to result), making a loss of
  parallelism look like an improvement. Catch2 3.4.0 hardcodes
  `Detail::Clock = std::chrono::steady_clock`, so this needs patching the
  dependency, not a flag.
- **PMU cycles/instructions** via `perf`: hosted runners are VMs and do not
  expose the PMU.
- **Sample-level A/B alternation**: Catch2 times all samples of one benchmark in
  a single process, so going finer than per-round alternation would cost one
  process per sample, and engine setup would dominate the measurement.

## Consequences
- Gate wall time stays roughly flat, but per-filter process count grows from 2
  to `2 x rounds`; startup and warmup overhead is now paid `rounds` times.
- A single stolen-CPU round no longer moves the point estimate; it widens the
  interval instead, which can still trigger the retry pass (by design) but not a
  failure.
- Absolute milliseconds from this gate are not comparable with the blocked
  measurements quoted in ADR-0008 or in older artifacts.
- Thresholds (`--threshold`, `--max-relative-ci`, `--memory-threshold`) are
  untouched; this ADR changes how a sample is taken, not what counts as a
  regression.
- Interleaving cannot fix noise that is deterministic per binary (different
  machine code paths, cache footprint). Those still need profiling.

## Evidence
- Run `40349682250` on PR #50 (`60108b59`, unchanged Rust-free baseline vs head
  without deterministic RK4), 3 attempts, every failure exclusively on
  "Paired Performance Gate":
  - attempt 1: base (unmodified code) showed `2D run()` at +/-60.2% and
    `stochasticStep [baseline]` at +/-31.1%;
  - attempt 2: `applyCommonPopulationStep` at 9.444 ms vs 4.251 ms in attempt 3,
    i.e. the host changed between attempts;
  - `stochasticStep [mutations: 8 types]` reported +3.3%, +4.8%, +5.6% across
    attempts — a consistent signal that the blocked schedule kept burying under
    whatever else the host was doing.
- Order flipping and median aggregation: `CellEvoX/tests/benchmarks/ci_benchmark_gate.py`
  (`aggregate_rounds`, `run_group`); invocation in `.github/workflows/ci.yml`.
