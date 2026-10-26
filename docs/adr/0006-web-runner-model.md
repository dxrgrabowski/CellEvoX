# ADR-0006: Single global runner with isolated batch outputs
Status: Proposed · 2026-10-24

## Context
The simulator is a CPU-saturating native subprocess, not a request-scoped
workload. Concurrency does not by itself corrupt results (RNG is counter-based),
but unrestricted concurrency oversubscribes the host, changes wall-clock
behaviour, and makes comparisons noisy. Batch items also start within the same
second and would collide on timestamped directories unless outputs are isolated.
## Decision
The backend holds one global job and returns HTTP 409 for new single runs while
it is active. Launch configs are written atomically and passed by absolute path.
Batch items get a unique `output_path/<batch_label>/...` base. Parallelism is
explicit and paired with `--threads-per-run`.
## Consequences
- Do not make the backend multi-tenant.
- Keep `parallel × threads-per-run` near the core count; the runner also pins a
  per-process thread limit.
- Relative `output_path` values resolve against the repository root.
- Do not rely on concurrency changing simulation results; only resource
  contention and output collisions are the reasons for these limits.
## Evidence
To be recorded before this ADR moves to `Accepted` (409 behaviour, batch output
isolation, and the per-process thread-limit code pointer).
