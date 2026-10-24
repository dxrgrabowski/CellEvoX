# ADR-0001: Counter-based RNG for thread-count independence
Status: Accepted · 2026-10-24

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
