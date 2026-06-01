# Run Output and Analysis

CellEvoX writes a timestamped run directory, then uses C++ and Python analysis
paths to produce CSV, plots, phylogeny, and animations.

## Run directory layout

A typical run created from `output_path` looks like:

```text
output/
  2026-05-31_22-30-00/
    config.json
    statistics/
      generational_statistics.csv
      memory_log.csv
    population_data/
      population_generation_<generation>.bin
      population_generation_<generation>.csv
    phylogeny/
      phylogenetic_tree.csv
      phylogenetic.gexf
      clone_tree.png
    general_plots/
    mutation_histograms/
    vaf_diagrams/
    muller_plots/
    visualizations/
      clone_growth_2d.mp4 or .gif
      tumor_growth_3d.mp4 or .gif
```

`RunDataEngine::prepareOutputDir()` appends a timestamp to the configured
`output_path` and copies the launch config to `config.json` when possible.

## Population snapshots

Primary writer/reader:

- `CellEvoX/include/io/PopulationSnapshotIO.hpp`

Python mirror:

- `CellEvoX/scripts/snapshot_io.py`

Current binary format:

- Magic: `CELXPOP1`
- Version: `2`
- Header size: `48` bytes
- Record size: `36` bytes
- Mutation payload record size: `5` bytes
- Supports spatial dimension marker.
- Supports driver-only mutation payloads and full mutation payloads.
- Reader also handles v1 snapshots and a legacy raw 3D record fallback.

2D snapshots write invalid/NaN position fields and `spatial_dimensions == 0`.
3D snapshots write valid positions and `spatial_dimensions == 3`.

By default, snapshot payloads include driver mutations only. With
`full_mutation_payload: true`, snapshots and CSV export include full mutation
payloads.

## Statistics

`statistics/generational_statistics.csv` is exported from
`run->generational_stat_report` and contains:

- tau/generation,
- total living cells,
- mean fitness,
- fitness variance/skewness/kurtosis,
- mean mutations,
- mutation variance/skewness/kurtosis.

`statistics/memory_log.csv` is written during simulation by each engine's memory
logging path. Current memory logging reads `/proc/self/statm`, so it is
Linux-oriented.

## Analysis mode

The executable supports:

```bash
./build/bin/CellEvoX --analyze /path/to/run_dir
```

Analysis mode:

1. Opens an existing run directory.
2. Detects population CSV or binary snapshots.
3. Exports CSV companions from binary snapshots if needed.
4. Runs Muller, clone phylogeny, clone count/lifespan, 2D animation, and 3D replay paths.

## Python scripts

Scripts live in `CellEvoX/scripts/`.

| Script | Role |
| --- | --- |
| `snapshot_io.py` | Shared loader for CSV/binary snapshots and clone utilities |
| `plot_muller.py` | Muller diagrams, relative and absolute |
| `plot_phylogeny.py` | Clone and sampled-cell phylogeny plots |
| `plot_clone_counts.py` | Clone count over time |
| `plot_clone_lifespans.py` | Clone lifespan distributions |
| `animate_clone_growth_2d.py` | 2D clone-growth animation |
| `visualize_tumor_3d.py` | 3D tumor replay, with PyVista preferred and Matplotlib fallback |
| `generate_test_data.py`, `test_radial.py` | Utility/demo scripts |

`RunDataEngine` resolves scripts from a few locations, including the source tree
and `/workspaces/CellEvoX/CellEvoX/scripts`.

## Web results parsing

The web backend reads outputs through `web/backend/results_parser.py`.

It scans for directories containing `config.json`, ignores `node_modules`, `.git`,
and `.venv`, and exposes:

- run list,
- config,
- summary,
- statistics rows,
- population CSV rows,
- Muller data for Plotly rendering.

Population endpoint reads CSV snapshots only. If a run has only binary snapshots,
use analysis/export first or rely on the C++ pipeline to create CSV companions.

## Integration risks

- Binary snapshot changes require synchronized C++, Python, tests, and web parser
  changes.
- Some older Python command invocations in `RunDataEngine.cpp` do not quote paths,
  while newer animation calls do. Paths containing spaces can still be risky.
- `memory_log.csv` can become a performance bottleneck when flushed too often.
- `ResultsParser` caps returned population generations to avoid huge responses.
- Web results parsing depends on the output directory shape and CSV column names.

## Verification checklist

For output changes:

1. Run unit tests that touch `PopulationSnapshotIO` and `RunDataEngine`.
2. Create at least one 2D and one 3D snapshot if the format changes.
3. Confirm `snapshot_io.py` can read the produced files.
4. Confirm web results endpoints still return JSON for stats/population/Muller data.
5. Update this doc and [Config fields by mode](config-fields-by-mode.md) if payload
   flags or output options changed.
