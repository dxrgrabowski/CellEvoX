# ADR-0001: Counter-based RNG for thread-count independence
Status: Accepted · 2026-10-26

## Context
A shared sequential RNG makes draws depend on TBB scheduling and chunking, so
results change with thread count and scientific comparison becomes invalid.
## Decision
Every draw is a pure hash of `(seed, step, cell_id, stream)`, where the stream
identifies the decision (death, birth, mutation; the spatial layer has its own
seeded stream).
## Consequences
- Never add a shared mutable RNG to the event step.
- Stream ids, step derivation, and id assignment order are contract.
- A determinism regression is a correctness bug, not a performance trade-off.
- Identical seed and configuration reproduce identical event draws regardless of
  thread count. Bit-identical artifacts across *different builds or hosts* are a
  verification goal, not a promise: floating-point results still depend on
  compiler flags (`-march=native`, FMA), the math library, and reduction order.
  Portable builds (`-DCELLEVOX_NATIVE_OPTIMIZATIONS=OFF`) are the closest
  approximation to cross-host agreement and the only meaningful setting for
  comparing machines.

## Evidence
- Hash implementation: `CellEvoX/include/utils/DeterministicRng.hpp`
  (`splitMix64` + `mix(seed, step, cell_id, stream)`).
- Thread-count independence: `CellEvoX/tests/test_simulation.cpp`, test cases
  "SimulationEngine stochastic population step is deterministic across worker
  counts" (1 vs 4 threads) and "SimulationEngine3DCapacity mechanics are
  deterministic across worker counts" (1 vs 5).
- Cross-host bit-identity is *not* evidenced; the floating-point caveat above is
  the accepted limit of this decision.
