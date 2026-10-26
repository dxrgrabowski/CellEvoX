# ADR-0006: Single global runner with isolated batch outputs
Status: Accepted · 2026-10-26

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
- Single-job behaviour: `web/backend/main.py` returns HTTP 409 for a second
  simulation or analysis request while one is active.
- Per-process thread cap: `web/backend/runner.py` sets `CELLEVOX_TBB_THREADS`
  (and passes `--threads` where supported) for both simulation and analysis.
- Batch output isolation: `web/backend/runner.py` derives a per-item
  `output_path/<batch_label>/...` base from the manifest.
