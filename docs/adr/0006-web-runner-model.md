# ADR-0006: Single global runner with isolated batch outputs
Status: Accepted · 2026-10-24

## Context
The simulator is a CPU-saturating native subprocess, not a request-scoped
workload; concurrent jobs would oversubscribe the host and make results
irreproducible. Batch items also start within the same second, colliding on
timestamped directories.
## Decision
The backend holds one global job and returns HTTP 409 for new single runs while
it is active. Launch configs are written atomically and passed by absolute path.
Batch items get a unique `output_path/<batch_label>/...` base. Parallelism is
explicit and paired with `--threads-per-run`.
## Consequences
- Do not make the backend multi-tenant.
- Keep `parallel × threads-per-run` near the core count.
- Relative `output_path` values resolve against the repository root.
