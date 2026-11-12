# ADR-0011: Deterministic instruction-count gate
Status: Accepted · 2026-11-12

## Context
ADR-0008's paired wall-clock verdict failed unchanged code on hosted runners, and
ADR-0009's interleaving only narrowed the noise. Catch2 3.4.0 pins
`std::chrono::steady_clock`, hosted runners expose no PMU, and process CPU time
would count parallel work as an improvement, so time is the wrong signal on this
infrastructure (#70).

## Decision
Retired instructions (`Ir`) measured by Valgrind Callgrind block; wall-clock
verdicts are advisory.

- `CellEvoX/tests/perf_probe.cpp` replays six `[perf-core]`-shaped workloads with
  fixed iteration counts, a fixed seed and one TBB thread, and prints a FNV-1a
  checksum of the state it produced.
- `CellEvoX/tests/benchmarks/perf_ir_gate.py` measures each side twice per
  workload and fails on an `Ir` increase above 2% (or above its named
  `--accepted-delta`), differing checksums (the binaries no longer do the same
  work), a spread above 2% between repeats (the workload is not deterministic),
  or an unrunnable/unparsable measurement.
- Reproducibility rests on two bounds: collection is toggled on `main`
  (`--collect-atstart=no --toggle-collect=main`), and every measurement starts
  from a stats directory that does not exist yet.
- The probe builds with `-O3` and without `-march=native`
  (`CellEvoX/tests/perf_probe.cmake`). `Ir` is only comparable within one
  compiler and Valgrind pair, so base and head are built in the same job.
- The wall-clock gate still runs, with `--advisory-timing`: its verdicts are
  reported but no longer fail the job. The 10% peak-RSS ceiling from ADR-0008
  still blocks.

## Consequences
- A regression now has a reproducible signature; no retry decides the job.
- Blind spot: the same work done slower (locality, branch misses) passes. The
  advisory report is what surfaces that; neither signal alone is a verdict.
- `--threshold 0.02` is headroom over a measured floor of 0.0000%, not tolerance
  for a known cost. Raising it to admit a known cost is forbidden by
  `AGENTS.md` (Negative Space), and it would be invisible in the diff. A cost
  the project deliberately accepts is named instead:
  `--accepted-delta realistic-2d-450=0.03` relaxes that single workload and
  prints `OK (accepted +3.0%)`, so the exception is written in the accepting
  PR's `ci.yml` instead of silently widening the gate. The exception does not
  expire: it stays in `ci.yml` after merge, so the accepting PR must also record
  the follow-up that removes it, or later PRs inherit the headroom.
- Callgrind is 20-50x slower than native, so the probe covers small populations
  and short runs; long scenarios stay in the advisory gate.
- Adding a workload means editing `perf_probe.cpp` and `WORKLOADS` in
  `perf_ir_gate.py`; unknown names are rejected, not skipped.
- Baseline-moving events land as one coordinated change: a snapshot ABI version
  bump alters the bytes the probe hashes (so it reports `SEMANTICS CHANGED`, not
  a regression), and fixing the `tau_step = 0.005` tech debt moves the `Ir` floor.
  Either PR must update this Evidence table and, if a workload is deliberately
  allowed to move, name it in `--accepted-delta` rather than widen `--threshold`.
- The base side is `github.event.pull_request.base.sha` (the base branch tip,
  fetched at depth 1), not the merge base. On a branch that is behind `main`
  the comparison therefore includes `main`'s newer commits as well, so rebase
  before investigating a failure on a long-lived branch.
- The job keeps `timeout-minutes: 10`: the gate costs 45-60 s, funded by running
  the advisory schedule at `3 rounds x 3 samples` instead of `7 x 5`. That
  ceiling was measured with a warm ccache; a cold cache rebuilds three targets
  for two trees and can approach it. The ccache key restores from `main`, so
  later runs start warm. A cold-cache timeout is an accepted, rerunnable
  infrastructure failure, not a verdict and not a bug to investigate. Because the
  timeout is job-level it skips both gates and the report upload, so a timed-out
  run is never a pass: rerun it instead of reading the absence of a verdict as
  success. The hard 10-minute ceiling is issue #70's constraint and this is its
  accepted trade-off. If cold-cache timeouts recur in practice, the follow-up is
  to split the advisory wall-clock gate into its own job so the blocking
  instruction gate is not starved by the timing schedule.

## Evidence
- Same binary as base and head: `+0.00%` everywhere, and both repeats of each
  side returned the identical count (spread `0.000000`).

  | Workload | Ir (both sides, both runs) |
  | --- | --- |
  | `stochastic-step-baseline` | 284,636,107 |
  | `stochastic-step-mutations` | 284,834,551 |
  | `stochastic-step-pressure` | 282,512,490 |
  | `run-5-steps` | 185,630,027 |
  | `common-step-baseline` | 42,678,646 |
  | `realistic-2d-450` | 290,202,150 |

  Absolute counts are host- and Valgrind-specific (a CI run read ~300M for the
  same workload); only the base/head ratio and the repeat spread mean anything.
- Two nondeterminism sources had to be removed first: pre-`main` dynamic-loader
  work varied up to 3.7% between processes (a `callgrind_annotate` diff confined
  it to `ld-linux`, with `main` byte-identical), and reusing the probe's stats
  directory moved counts by 0.4-3.6%.
- Verdict logic, checked by feeding fabricated counts to `evaluate()`: `+3%` →
  `REGRESSED`, `+1%` → `OK`, `-3%` → `IMPROVED`, `+3%` with that workload's
  `--accepted-delta realistic-2d-450=0.035` → `OK (accepted +3.5%)`, with
  `=0.025` → `REGRESSED`, and an exception naming another workload does not
  apply; a differing checksum → `SEMANTICS CHANGED`, a 3% repeat spread →
  `NON-DETERMINISTIC`.
- Advisory timing and RSS, checked through
  `ci_benchmark_gate.has_failure(..., advisory_timing=True)`: timing
  `REGRESSED`/`UNSTABLE` does not block, RSS `REGRESSED` does; RSS `+15%` is
  `REGRESSED` and `+8%` is `OK` against the 10% ceiling. In CI (`43086799157`)
  the run reported `1 unstable` and still ended `CI benchmark gate passed
  (timing verdicts were advisory)`, with `Memory RSS ... +0.0% | OK`.
- Artifacts: the `benchmark-reports` artifact of `43086799157` contains
  `perf-ir-gate.json` and `perf-gate.json`, and the gate's JSON records the
  thresholds it ran with (`accepted_delta: {}` when no exception is given).
- Checksum coverage was widened to the outputs the engine only logs on failure:
  lineage node contents (`parent_id`, `child_sum`, `death_time`) and the bytes of
  every population snapshot file. Two `realistic-2d-450` runs into separate stats
  directories returned the identical checksum `0x3779bbec57a40b7b`, so hashing
  file bytes did not reintroduce nondeterminism. A missing snapshot file throws
  and the probe exits 2, which the gate reports as `MISSING` rather than a
  smaller instruction count. Malformed numeric arguments now exit 2 instead of
  terminating on an uncaught exception.
- Full gate: 56 s locally for six workloads x two sides x two runs; in CI
  (`43086799157`) 36 s for the gate and 161 s for `bench-regression-check`.
- Code: `CellEvoX/tests/perf_probe.{cpp,cmake}`,
  `CellEvoX/tests/benchmarks/perf_ir_gate.py`,
  `CellEvoX/tests/benchmarks/ci_benchmark_gate.py` (`--advisory-timing`),
  `.github/workflows/ci.yml`.
