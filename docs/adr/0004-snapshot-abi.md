# ADR-0004: Binary snapshot ABI and backward compatibility
Status: Accepted · 2026-10-24

## Context
Snapshots are written by C++ and read by Python analysis scripts and the web
layer. Published runs and older files must remain readable after upgrades.
## Decision
Format is `CELXPOP1` v2: 48-byte header, 36-byte cell record, 5-byte mutation
payload. Readers keep v1 and the legacy raw 3D fallback. 2D encodes NaN positions
and dimension 0; 3D encodes positions and dimension 3.
## Consequences
- Changes are additive and synchronized across C++, `snapshot_io.py`, tests, and
  the web parser.
- Layout changes need a version bump plus reader fallback.
- Cadence uses integer generation indices to avoid float drift; the final
  snapshot may overwrite the last periodic file with the same index.
