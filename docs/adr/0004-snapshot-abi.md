# ADR-0004: Binary snapshot ABI, one coherent contract per commit
Status: Proposed · 2026-10-24

## Context
Snapshots are written by C++ and read by Python analysis scripts and the web
layer. The project is early-stage and under active development, so **backward
compatibility of run artifacts is neither supported nor guaranteed**: keeping
every historical layout readable would add code that is never used. A contract
must still be *coherent* — every reader in the repository has to agree with the
writer at any given commit.
## Decision
Format is `CELXPOP1` v2: 48-byte header, 36-byte cell record, 5-byte mutation
payload. Reader support is intentionally asymmetric: the C++ reader accepts v2,
plus v1 and the legacy raw 3D record stream as **removable legacy fallbacks**;
`snapshot_io.py` accepts v2 only and prefers CSV when both formats exist; the
web backend consumes population CSV, not the binary ABI. 2D encodes NaN
positions and dimension 0; 3D encodes positions and dimension 3.

The intended enforcement of this contract is a single cross-language fixture
(`tests/fixtures/mini_run/`) read by both the C++ and the Python tests, so that
editing one side cannot silently diverge from the other. That fixture does not
exist yet (tracked as the P1 "one executable contract" item); until it lands,
the contract is only as strong as the duplicated readers.
## Consequences
- A writer change requires a version bump and synchronized updates across C++,
  `snapshot_io.py`, tests, and the CSV export that the web backend reads. Old
  runs do **not** have to stay readable.
- The Python reader is allowed to reject unknown versions rather than guess; the
  C++ v1/raw-3D fallbacks may be deleted without an ADR, since no compatibility
  guarantee backs them.
- Cadence uses integer generation indices (`floor(tau + epsilon)`) to avoid float
  drift, deduplicated per index. Snapshots are periodic only: there is no forced
  terminal write, so the last snapshot can predate the end of the run. Terminal
  state is authoritative in `run_metadata.json` and the phylogeny.

## Evidence
- ABI sizes are pinned by `static_assert`s in
  `CellEvoX/include/io/PopulationSnapshotIO.hpp`.
- Reader matrix exercised by `CellEvoX/tests/test_population_snapshot_io.cpp`
  (v2 round-trip, v1, legacy raw 3D) and `CellEvoX/scripts/snapshot_io.py`
  (v2 only).
- Cross-language fixture: not present yet (no `tests/fixtures/` directory); the
  P1 item is the remaining blocker for this ADR moving to `Accepted`.
