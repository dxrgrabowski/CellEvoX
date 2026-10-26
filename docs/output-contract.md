# Output contract

This file is the one place where the *shape* of run outputs is documented. These
names and columns are a public contract: analysis scripts, the web backend, and
downstream notebooks depend on them. Internal code structure belongs in
[AGENTS.md](../AGENTS.md); the reasons behind the design belong in
[ADRs](adr/README.md).

## Run directory

Every run gets a timestamped directory below the configured `output_path`:

```
output/<YYYY-MM-DD_HH-MM-SS>/
├── config.json
├── run_metadata.json
├── population_data/
│   ├── population_generation_<N>.csv
│   └── population_generation_<N>.bin      # optional binary ABI snapshot
├── statistics/
│   └── generational_statistics.csv
├── phylogeny/
│   └── phylogenetic_tree.csv
├── muller_plots/                          # post-processing
├── vaf_diagrams/
├── mutation_histograms/
├── general_plots/
└── visualizations/                        # MP4/GIF animations
```

`config.json` is a copy of the effective configuration. Analysis helpers also
accept it from the parent directory, so a single config can cover a batch
output tree.

## `run_metadata.json`

| Field | Meaning |
| --- | --- |
| `schema_version` | Metadata schema version (currently `1`). |
| `termination_reason` | `planned_horizon`, `population_cutoff`, or `shutdown_requested`. |
| `termination_verified` | `true` only after the terminal state was checked against the horizon/cutoff. |
| `terminal_time` | Simulation time (tau) at termination. |
| `terminal_population` | Live cell count at termination. |
| `configured_horizon` | `steps × tau_step` from the configuration. |
| `population_cutoff` | Configured `max_population_cutoff`, `0` when disabled. |

The writer refuses to emit metadata that contradicts the engine's terminal
state; a missing or inconsistent `run_metadata.json` is a run failure, not an
empty run.

## Population CSV

Header, in this order:

```
CellID,ParentID,Fitness,MutationCount,Mutations,X,Y,Z,PositionValid,SpatialDimensions
```

- `ParentID` is the cell that divided to produce this cell. A birth removes the
  parent, so it does not appear in later snapshots. `0` marks a root cell of the
  initial population.
- `MutationCount` is the full number of mutations carried by the cell, even when
  the `Mutations` column is filtered.
- `Mutations` is a space-separated list of `(mutation_id,mutation_type)` pairs.
  The `mutation_id` identifies the cell in which that mutation originated; it is
  not the current cell ID and not the parent ID. With
  `full_mutation_payload: false` the column contains driver mutations only;
  setting it to `true` exports the full payload and also widens the VAF
  histograms to all mutations.
- 2D runs emit `X,Y,Z = 0` with `PositionValid = 0` and
  `SpatialDimensions = 0`. 3D runs emit real coordinates with
  `PositionValid = 1` and `SpatialDimensions = 3`. Consumers must key off
  `PositionValid`/`SpatialDimensions`, not off zero coordinates.

## Binary snapshot ABI (`CELXPOP1`)

Current version is **v2**:

- 48-byte header: magic `CELXPOP1`, version, record size, tau, record count,
  mutation payload count, spatial dimensions, mutation record size, flags.
- 36-byte cell record.
- 5-byte mutation payload entry (`mutation_id`, `mutation_type`), addressed by
  per-record offset/count.

Payload flags distinguish driver-only (`0x1`) from full (`0x2`) payloads. The
same records are materialized as CSV; CSV is the interchange format for
analysis and for the web backend.

### Reader compatibility

| Reader | Accepts |
| --- | --- |
| C++ (`PopulationSnapshotIO.hpp`) | v2, v1, legacy raw 3D record streams |
| Python (`CellEvoX/scripts/snapshot_io.py`) | v2 only; prefers CSV when both exist |
| Web backend (`web/backend/results_parser.py`) | population CSV only |

So a writer change must stay additive and be mirrored in C++, the Python reader,
the tests, and the CSV export. A layout change needs a version bump plus a
reader fallback in C++; the Python reader deliberately fails loudly on unknown
versions instead of guessing.

## Snapshot cadence and terminal state

Snapshots are written on integer generation indices computed as
`floor(tau + epsilon)`, only when the index is a multiple of the configured
population resolution. There is **no forced write at the end of a run**, and
`--analyze` does not add one. Therefore:

- the last snapshot can predate the end of the run;
- the terminal population is only authoritative in `run_metadata.json`
  (`terminal_time`, `terminal_population`) and in the phylogeny
  (`DeathTime` / final node state), not in the population CSV.

Consumers that need the final population must read `run_metadata.json` rather
than extrapolating from the last snapshot.
