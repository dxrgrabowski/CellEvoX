# ADR-0010: Deterministic instruction-count gate for performance verdicts
Status: Accepted · 2026-11-12

## Context
ADR-0008 made a paired wall-clock comparison the blocking performance check, and
ADR-0009 interleaved the rounds to survive a shared runner. Interleaving lowered
the noise floor but did not remove it: unchanged code still produced different
verdicts between attempts, because a GitHub-hosted VM gives no control over CPU
steal, frequency, cache state or neighbours. A gate that fails unchanged code
teaches the team to ignore it, and a failure that has to be retried is not
evidence.

The obvious escapes are closed. Catch2 3.4.0 hardcodes
`Detail::Clock = std::chrono::steady_clock`, and hosted runners do not expose
the PMU (both recorded in ADR-0009).

## Decision
The blocking performance signal is the number of retired instructions (`Ir`)
executed by a fixed workload, measured with Valgrind Callgrind. Wall-clock
timings are still measured and reported, but they are advisory: only the
instruction count and the peak-RSS ceiling can fail the job.

- `CellEvoX/tests/perf_probe.cpp` runs six workloads mirroring the `[perf-core]`
  benchmarks with explicit iteration counts, a fixed seed and
  `tbb::global_control` pinned to one thread, and prints a FNV-1a checksum of
  the resulting state.
- `CellEvoX/tests/benchmarks/perf_ir_gate.py` measures each side twice per
  workload and blocks on an `Ir` increase above 2%, differing checksums (the two
  binaries no longer perform the same work), a spread above 2% between repeats
  (the workload is not deterministic), or anything unrunnable/unparsable.
- Two bounds are what make the count reproducible:
  collection is toggled on the probe's `main`
  (`--collect-atstart=no --toggle-collect=main`), and every measurement starts
  from a directory that does not exist yet.
- The probe is compiled with `-O3` and without `-march=native`. `Ir` is only
  comparable within one compiler and Valgrind pair, which is why base and head
  are built in the same job.

## Consequences
- The gate does not measure time, so work cannot be moved out of the measured
  window to "improve" the number. The complementary blind spot is real: the same
  work done slower (worse locality, more branch misses) passes this gate while
  the advisory report shows the wall-clock regression. Neither signal alone is a
  complete verdict.
- `--threshold 0.02` is not a tolerance for a known regression; it is headroom
  above a measured noise floor of 0.0000% for genuine codegen drift elsewhere in
  the workload.
- Callgrind runs 20-50x slower than native, so the probe uses small populations
  and short step counts. Long scenarios stay in the advisory wall-clock gate.
- Adding a workload means editing both `perf_probe.cpp` and `WORKLOADS` in
  `perf_ir_gate.py`; the gate rejects unknown workload names instead of
  skipping them.
- `-march=native` remains the default for normal builds (ADR-0007); the probe is
  the one target that opts out. A missing probe binary fails the gate.
- The `bench-regression-check` job keeps a hard `timeout-minutes: 10`. The
  instruction gate costs about a minute, which is funded by shrinking the
  now-advisory wall-clock schedule from `7 rounds x 5 samples` to
  `3 rounds x 3 samples` and dropping `--fail-on-inconclusive`.

## Evidence
- Same host, same binary supplied as base and head: every workload reported
  `+0.00%`, and each side reported the identical count on both of its runs
  (spread `0.000000`):

  | Workload | Ir (both sides, both runs) | checksum |
  | --- | --- | --- |
  | `stochastic-step-baseline` | 284,636,107 | `d63c3d137b1bafa7` |
  | `stochastic-step-mutations` | 284,834,551 | `38168ffb21cd9238` |
  | `stochastic-step-pressure` | 282,512,490 | `b407b8ea55a99eef` |
  | `run-5-steps` | 185,630,027 | `131f7a097bc5738d` |
  | `common-step-baseline` | 42,678,646 | `15f191647e90472b` |
  | `realistic-2d-450` | 290,202,150 | `aca08d8eac53c3a6` |

  Absolute counts depend on the host and the Valgrind build, so only the
  base/head ratio and the repeat spread are meaningful; both sides are always
  measured in the same job. An earlier run of the same pair on this host
  produced the same exact-match result, with counts about 0.02% apart from the
  table.
- Nondeterminism that had to be removed to reach that:
  - pre-`main` dynamic-loader work (symbol lookup, relocation) varied by up to
    3.7% between processes. A `callgrind_annotate` diff confined the delta to
    `ld-linux` frames while `main` and `applyCommonPopulationStep` were
    byte-identical.
  - the probe's stats-directory cleanup cost depends on what the previous
    measurement left behind, which moved counts by 0.4-3.6% when a directory was
    reused.
- Full gate runtime for six workloads x two sides x two runs: 56 s.
- Job runtime before this change: 6m12s (`43008809170`), of which the wall-clock
  gate took 4m25s on the `7 x 5` schedule.
- Code: `CellEvoX/tests/perf_probe.cpp`, `CellEvoX/tests/perf_probe.cmake`,
  `CellEvoX/tests/benchmarks/perf_ir_gate.py`,
  `CellEvoX/tests/benchmarks/ci_benchmark_gate.py` (`--advisory-timing`) and
  `.github/workflows/ci.yml`.
