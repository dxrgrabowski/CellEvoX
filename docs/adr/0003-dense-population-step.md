# ADR-0003: Dense stochastic population step with an optional xyz overlay
Status: Accepted · 2026-11-04

## Context
CellEvoX supports two simulation modes: a non-spatial stochastic population model
and that same model with an xyz spatial overlay. The base model does not calculate
X/Y coordinates, so calling it "2D" confuses the model with its visualizations.
Both modes need identical birth, death, mutation and lineage semantics.

Population storage is a performance decision: the measured dense-array step was
1.8–3.0x faster than the equivalent hash-map step even with cached alive ids.
Sharing the slower implementation would sacrifice the base model's throughput.

## Decision
Both modes are supported foundations of the simulator:

- **Non-spatial stochastic tau-leap** (`stochastic`).
- **Stochastic + xyz spatial overlay** (`spatial_3d_capacity`).

Use one stochastic population step on dense storage for both modes. XYZ adds
positions, a spatial hash grid and mechanical relaxation after population events;
it must not affect event probabilities or lineage semantics. The dense step is
the normal base-model step, not a separate engine variant.

Share the population algorithm and storage without forcing the run drivers,
reporting or spatial mechanics into a common framework. Performance takes
priority over further architectural uniformity. Keep dense batching, reusable
slots and direct id lookup; collect event results only when the caller needs
them for the overlay. Further abstraction needs a concrete extension need and
paired benchmark evidence.

## Consequences

- Change event/RNG semantics in one place and retain base/XYZ parity and
  thread-count determinism tests.
- Keep `CellMap`/`Graveyard` materialization at output and pruning boundaries;
  do not rebuild hash maps on each population step.
- Preserve RNG streams, daughter ordering, id assignment and snapshot formats.
- Use "non-spatial" for the base model. "2D" may still describe a plot or
  visualization; the configuration names above remain unchanged.
- Do not add a second population loop for a spatial overlay. Extensions that
  change biological event semantics need a separate domain decision.
- Tuning constants are heuristics, not invariants. Check changes against the
  paired performance gate in ADR-0008.

## Evidence

- Dense population storage: `CellEvoX/include/systems/DensePopulation.hpp`.
  Both engine drivers call `applyCommonPopulationStep` from
  `CellEvoX/include/systems/CommonPopulationStep.hpp`; the non-spatial caller
  omits event-result collection at compile time.
- `CellEvoX/tests/test_simulation.cpp`: "SimulationEngine3DCapacity matches
  non-spatial population events"; worker-count determinism tests cover both
  modes. `test_common_population_step.cpp` checks event payloads and slot reuse.
- The consolidation also preserved six base/XYZ snapshot files byte for byte,
  including mutation payloads and coordinates, against the pre-change binary.
- A Windows/MSVC 19.44 Release comparison against `1aa36a0`, with seven
  alternating base/head rounds, 10 samples and 200 ms warm-up, kept core-suite
  medians within the 5% time budget. Timing-suite medians on the same four
  logical processors ranged from -33.9% to +0.6%; median peak working set changed
  by less than 0.01%. Unrestricted desktop timings were variable, including a
  short-run slowdown that did not reproduce with fixed affinity; these local
  measurements are diagnostic, and the Linux paired CI gate remains required.
- Before consolidation, an isolation benchmark on AMD Ryzen 5 5500 (10 hardware
  threads), g++ 15.2.0, Release with native optimization, measured five steps per
  independently warmed state (10 Catch2 samples, 50 ms warm-up). The hash-map
  step used the same seed/event rules and a prebuilt alive-id cache:

  | Population / pressure | Dense step | Hash-map step, cached ids | Dense speedup |
  | --- | --- | --- | --- |
  | 100k low | 0.92 ms | 1.65 ms | 1.79x |
  | 100k high | 0.55 ms | 1.62 ms | 2.95x |
  | 1M low | 5.49 ms | 16.32 ms | 2.97x |
  | 1M high | 8.49 ms | 23.70 ms | 2.79x |

  These are milliseconds per step and justify the storage choice; they are not
  a claim about the consolidated implementation's speedup. The active
  population-step benchmarks cover the shared dense step with and without
  event results at both sizes and pressures.
