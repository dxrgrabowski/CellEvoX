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
There is one coherent snapshot contract per commit: the layout (currently
`CELXPOP1` v2), its record sizes and flags, the reader matrix, and the cadence
are specified in [docs/output-contract.md](../output-contract.md) and must match
every reader in the repository at that commit. Backward compatibility of run
artifacts is **not** supported or guaranteed, so readers may reject unknown
versions instead of guessing and legacy fallbacks may be deleted.

The intended enforcement of this contract is a single cross-language fixture
(`tests/fixtures/mini_run/`) read by both the C++ and the Python tests, so that
editing one side cannot silently diverge from the other. That fixture does not
exist yet (tracked as the P1 "one executable contract" item); until it lands,
the contract is only as strong as the duplicated readers.
## Consequences
- A writer change requires a version bump and synchronized updates across C++,
  `snapshot_io.py`, tests, and the CSV export that the web backend reads. Old
  runs do **not** have to stay readable.
- The C++ v1/raw-3D fallbacks may be deleted without an ADR, since no
  compatibility guarantee backs them.
- Cadence and terminal-state rules are part of the same output contract; this
  ADR does not restate them.

## Evidence
- ABI sizes are pinned by `static_assert`s in
  `CellEvoX/include/io/PopulationSnapshotIO.hpp`.
- Round-trip and reader coverage in
  `CellEvoX/tests/test_population_snapshot_io.cpp` and
  `CellEvoX/scripts/snapshot_io.py`; the reader matrix itself is listed in the
  output contract.
- Cross-language fixture: not present yet (no `tests/fixtures/` directory); the
  P1 item is the remaining blocker for this ADR moving to `Accepted`.
- Owner decision: backward compatibility is explicitly neither supported nor
  guaranteed (recorded in the PR #45 review discussion, 2026-10-26). The
  `Proposed` status covers the missing fixture and enforcement, not the
  compatibility policy itself.
