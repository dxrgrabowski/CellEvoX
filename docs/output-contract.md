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
│   ├── generational_statistics.csv
│   └── memory_log.csv
├── phylogeny/
│   ├── phylogenetic_tree.csv
│   └── phylogenetic.gexf
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
  parent, so it does not appear in later snapshots. Every cell of the initial
  population is stored with the default `ParentID = 0`, and cell ID `0` is a real
  initial cell, so `ParentID == 0` means either "initial population" or "daughter
  of cell 0". It **cannot** be used to classify roots. Founders are the IDs `0`
  to `initial_population - 1` from the run configuration. No snapshot is taken at
  initialization and periodic snapshots are written after a completed step, so
  the initial population as a whole never appears in a population CSV, although a
  founder that never divided can still show up in later snapshots. Extinct
  lineages may be missing from the phylogeny as well: the tree is built from the
  terminal living cells and their ancestors, so the outputs are not a complete
  founder census.
- `MutationCount` is the full number of mutations carried by the cell, even when
  the `Mutations` column is filtered.
- `Mutations` is a space-separated list of `(mutation_id,mutation_type)` pairs.
  `mutation_id` is the cell in which that mutation was created: a newly acquired
  mutation is stamped with the daughter's `CellID`, so it **can** equal the
  current cell ID, and an inherited mutation can equal the current `ParentID`.
  Do not treat `mutation_id` as a unique ancestor discriminator. With
  `full_mutation_payload: false` the column contains driver mutations only;
  setting it to `true` exports the full payload and also widens the VAF
  histograms to all mutations.
- 2D runs leave `X,Y,Z` **empty** (`,,` in the CSV, `NaN` in the binary snapshot)
  with `PositionValid = 0` and `SpatialDimensions = 0`; a CSV reader such as
  pandas loads them as `NaN`, not `0`. 3D runs emit real coordinates with
  `PositionValid = 1` and `SpatialDimensions = 3`. Consumers must key off
  `PositionValid`/`SpatialDimensions`.

## Statistics files

`statistics/generational_statistics.csv` has one row per recorded generation:

```
Generation,TotalLivingCells,MeanFitness,FitnessVariance,FitnessSkewness,FitnessKurtosis,MeanMutations,MutationsVariance,MutationsSkewness,MutationsKurtosis
```

`Generation` stores the simulation time (`tau`) of the sample, not an integer
generation index.

`statistics/memory_log.csv` is appended during the run:

```
Tau,RSS_KB,Cells_Count,Graveyard_Count,Estimated_Cells_KB,Estimated_Graveyard_KB
```

## Phylogeny files

`phylogeny/phylogenetic_tree.csv` has one row per node:

```
NodeID,ParentID,ChildSum,DeathTime
```

`DeathTime == 0` marks a node that is still alive at termination, with one
exception: node `0` is preinserted as a synthetic root with `DeathTime = 0`, and
that value is not replaced when cell `0` dies. A non-zero value is the `tau` at
which the node died. Do not treat every `DeathTime == 0` row as a living cell.
There is no forced terminal snapshot, so the last population CSV may predate
`terminal_time` and list a different set of cells with the same count; only a
snapshot whose recorded tau equals `terminal_time` represents the terminal
population. Prefer the GEXF `status` attribute, which is computed from the
terminating cell set, and note that `terminal_population` alone cannot recover
terminal cell IDs. As with the population CSV, `ParentID == 0` does not
distinguish roots from daughters of cell 0.

`phylogeny/phylogenetic.gexf` is written by `exports` and `full` post-processing.
It is GEXF 1.3 with directed edges from parent to child and node attributes
`status` (`ALIVE`/`DEAD`), `parent_id`, and `child_sum`.

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
| C++ (`PopulationSnapshotIO.hpp`) | v2, plus v1 and legacy raw 3D as removable fallbacks |
| Python (`CellEvoX/scripts/snapshot_io.py`) | v2 only; prefers CSV when both exist |
| Web backend (`web/backend/results_parser.py`) | population CSV only |

Backward compatibility of run artifacts is **not guaranteed**: the project is
early-stage, so keeping every historical layout readable would add code that is
never used. What is required is coherence at a given commit — a writer change
needs a version bump and matching updates in C++, `snapshot_io.py`, the tests,
and the CSV export. Old runs do not have to stay readable; the Python reader
deliberately fails loudly on unknown versions instead of guessing.

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
