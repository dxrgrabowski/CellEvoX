# ADR-0004: Binary snapshot ABI, one coherent contract per commit
Status: Accepted · 2026-11-11

## Context
Snapshots are written by C++ and read by Python analysis scripts and the web
layer. The project is early-stage and under active development, so **backward
compatibility of run artifacts is neither supported nor guaranteed**: keeping
every historical layout readable would add code that is never used. A contract
must still be *coherent* — every reader in the repository has to agree with the
writer at any given commit.
## Decision
There is one coherent snapshot contract per commit: the layout (currently
`CELXPOP1` v3), its record sizes and flags, the reader matrix, and the cadence
are specified in [docs/output-contract.md](../output-contract.md) and must match
every reader in the repository at that commit. Backward compatibility of run
artifacts is **not** supported or guaranteed, so readers may reject unknown
versions instead of guessing and legacy fallbacks may be deleted.

The contract is enforced by a single cross-language fixture
(`CellEvoX/tests/fixtures/mini_run/`, added in #52) read by both the C++ and the
Python tests, so editing one side cannot silently diverge from the other.

## Consequences
- A change to the serialized layout, encoding, or interpretation requires a
  version bump and synchronized updates across C++, `snapshot_io.py`, tests, and
  the CSV export that the web backend reads. A writer refactor that emits the
  same bytes needs no bump. Old runs do **not** have to stay readable.
- The C++ v1/raw-3D fallbacks were removed under this policy (#52); no
  compatibility guarantee backs them, and both readers now accept v3 only.
- Cadence and terminal-state rules are part of the same output contract; this
  ADR does not restate them.

## Evidence
- ABI sizes are pinned by `static_assert`s in
  `CellEvoX/include/io/PopulationSnapshotIO.hpp`.
- Canonical cross-language fixtures in `CellEvoX/tests/fixtures/mini_run/`:
  a driver-only v3 snapshot, a full-payload v3 snapshot, their CSV export, and
  truncated/unknown-version files for the negative path (#52, bumped to v3 by
  [ADR-0010](0010-gamma-mutation-effect.md)).
- The same fixture is parsed by `CellEvoX/tests/test_population_snapshot_io.cpp`
  and `CellEvoX/scripts/test_snapshot_io.py`; the Python test also cross-checks
  the binary reader against the CSV export. The reader matrix itself is listed
  in the output contract.
- Owner decision: backward compatibility is explicitly neither supported nor
  guaranteed (recorded in the PR #45 review discussion, 2026-10-26).
- Owner decision: the bump is required by a change to the serialized layout,
  encoding, or interpretation, not by any edit of the writer, so a writer
  refactor that produces identical output needs none (owner clarification
  during the PR #65 review, 2026-11-10).
