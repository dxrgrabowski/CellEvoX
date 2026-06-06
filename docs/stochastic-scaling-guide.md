# Stochastic scaling guide

This guide summarizes the current practical behavior of the 2D stochastic engine
when it is run headlessly in large batches. It is based on local release builds
and short scaling probes from the `perf/stochastic-4thread-scaling` branch, so
treat the numbers as a starting point for a new machine rather than a universal
cluster constant.

Related docs: [batch runs](batch-runs.md), [development workflows](development-workflows.md), [simulation engines](simulation-engines.md).

## What scales well

The optimized stochastic step now parallelizes the expensive per-cell event
sampling path and keeps results deterministic across thread counts by deriving
random draws from stable `(seed, step, cell_id, stream)` keys. The largest gains
appear when the active population is high and each step has enough cell work to
amortize TBB scheduling and event-buffer merging.

Representative local step-only probes on a hybrid Intel Core Ultra 7 165H
machine showed this shape:

| Active cells | 1 thread | 4 threads | Speedup | 4-thread efficiency |
| --- | ---: | ---: | ---: | ---: |
| 50k | 0.65 s | 0.21 s | 3.1x | 76% |
| 200k | 0.93 s | 0.29 s | 3.2x | 80% |
| 1M | 1.23 s | 0.33 s | 3.7x | 93% |

The exact timings are noisy because the local CPU has mixed performance and
efficiency cores, but the trend was stable: large, event-heavy populations can
use 4 threads efficiently; small or cutoff-limited runs are usually better as
more independent processes with fewer threads per process.

## Recommended thread counts

For thesis-scale stochastic batches, prefer process-level parallelism first and
thread-level parallelism only when each process has enough active cells.

| Expected active population | Suggested threads per run | Batch strategy |
| --- | ---: | --- |
| up to 50k | 1-2 | Fill the node with many independent runs. Extra threads per run are usually less useful than more processes. |
| 50k-200k | 2 | Use 4 only after a pilot shows good speedup and memory/IO are comfortable. |
| 500k-1M+ | 4 | Good default for long, event-heavy runs; test 8 before using it widely. |
| very large exploratory runs | 4-8 | Benchmark first. Do not assume that 16, 32, or 128 threads help one run. |

On a 128-thread cluster node, start from these shapes:

| Workload | Starting command shape |
| --- | --- |
| many 50k-cutoff or short configs | `--parallel 96` to `--parallel 128`, `--threads-per-run 1`, if RAM allows |
| medium configs around 200k cells | `--parallel 48` to `--parallel 64`, `--threads-per-run 2` |
| large configs around 1M cells | `--parallel 24` to `--parallel 32`, `--threads-per-run 4` |
| uncertain memory or heavy exports | reduce `--parallel` first, then tune threads |

Keep `--parallel * --threads-per-run` at or below the useful hardware thread
budget, and leave some capacity for the OS, filesystem, and post-processing.
For shared nodes, a conservative first target is 80-90% of available hardware
threads rather than exactly 100%.

## Pilot protocol

Before launching a large grid on a new machine:

1. Pick one representative configuration from the batch.
2. Run a short pilot with `--threads 1`, `--threads 2`, and `--threads 4`.
3. Compare wall time, CPU utilization, peak RSS, and whether output/export time
   dominates simulation time.
4. Choose the lowest thread count that keeps efficiency high enough.
5. Scale `--parallel` up until CPU or RAM becomes the bottleneck.

For runs with `max_population_cutoff=50000`, expect process-level parallelism to
win in most cases. These runs stop before they become large enough for high
per-run thread counts to pay off.

## Post-processing caveats

`--postprocess none` measures the simulation path most cleanly. `exports` and
`full` add IO, CSV/GEXF generation, and optionally plots or animations. Those
stages can become the wall-clock bottleneck and should not be interpreted as
stochastic-step scaling.

When using `full_mutation_payload`, frequent population snapshots, or large GEXF
exports, budget RAM and filesystem bandwidth separately from CPU. If the node is
CPU-idle during export-heavy phases, increasing `--threads-per-run` will not fix
the bottleneck.

## Ideas worth checking next

- Repeat the 1/2/4/8-thread probes on the target cluster node type with fixed
  CPU affinity. The local hybrid laptop CPU is useful for development but not a
  reliable predictor for a homogeneous compute node.
- Add a dedicated no-output benchmark mode that reports only stochastic step
  timing. This would separate simulation throughput from progress printing,
  snapshots, memory logging, and post-processing.
- Benchmark 3D capacity separately. Spatial mechanics and capacity checks have
  a different cost profile than the 2D stochastic population step.
- If average efficiency above 90% is required for small and medium populations,
  the next gains are likely algorithmic: lower event-buffer merge overhead,
  fewer per-step passes over memory, and more cache-friendly mutation handling.
- For production cluster runs, consider scheduler-level CPU pinning and memory
  limits so independent processes do not migrate across sockets or compete with
  each other unpredictably.
