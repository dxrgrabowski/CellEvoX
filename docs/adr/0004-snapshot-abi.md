# ADR-0004: Binary snapshot ABI and backward compatibility
Status: Proposed · 2026-10-24

## Context
Snapshots are written by C++ and read by Python analysis scripts and the web
layer. Published runs and older files must remain readable after upgrades.
## Decision
Format is `CELXPOP1` v2: 48-byte header, 36-byte cell record, 5-byte mutation
payload. Reader support is intentionally asymmetric: the C++ reader accepts v2,
v1, and the legacy raw 3D record stream; `snapshot_io.py` accepts v2 only and
prefers CSV when both formats exist; the web backend consumes population CSV,
not the binary ABI. 2D encodes NaN positions and dimension 0; 3D encodes
positions and dimension 3.
## Consequences
- Changes are additive and synchronized across C++, `snapshot_io.py`, tests, and
  the CSV export that the web backend reads.
- Layout changes need a version bump plus a C++ reader fallback; the Python
  reader is allowed to reject unknown versions rather than guess.
- Cadence uses integer generation indices (`floor(tau + epsilon)`) to avoid float
  drift, deduplicated per index. Snapshots are periodic only: there is no forced
  terminal write, so the last snapshot can predate the end of the run. Terminal
  state is authoritative in `run_metadata.json` and the phylogeny.
## Evidence
To be recorded before this ADR moves to `Accepted` (ABI sizes, reader matrix,
and a run showing a final snapshot earlier than `terminal_time`).
