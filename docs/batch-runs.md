# Batch simulation runs

CellEvoX batch runs use the same launch path as a single web run: each config is written as an atomic launch JSON file and then passed to the C++ binary with `--config`.

Related docs: [docs index](README.md), [architecture](architecture.md), [development workflows](development-workflows.md).

## Console

Run several config files sequentially:

```bash
python3 web/backend/run_batch.py param_set_1_stochastic.json param_set_2_stochastic.json
```

Run a manifest:

```bash
python3 web/backend/run_batch.py --manifest batch_manifest.json
```

Run several configs concurrently while preventing TBB oversubscription:

```bash
python3 web/backend/run_batch.py --manifest batch_manifest.json \
  --parallel 4 \
  --threads-per-run 5 \
  --postprocess exports
```

`--parallel` controls how many independent simulation processes are active at once.
`--threads-per-run` is forwarded to the C++ binary as `--threads` and
`CELLEVOX_TBB_THREADS`, capping TBB inside each process. For a 22-core machine,
start with combinations whose product is near the available core count, for
example `--parallel 4 --threads-per-run 5` or `--parallel 6 --threads-per-run 3`,
then benchmark a small representative batch.

`--postprocess` can be:

- `full`: current behavior, including plots and visualizations.
- `exports`: write CSV/GEXF outputs but skip expensive plot and animation steps.
- `none`: run the simulation and keep in-run artifacts such as binary population
  snapshots and memory logs, skipping post-run exports and plots.

For multi-run batches, the runner isolates each config under a unique
`output_path/<batch_run_label>/...` base directory before the C++ timestamped run
directory is created. This avoids timestamp collisions when many simulations
start in the same second.

Runs launched with `--postprocess none` still appear in the Results view when a
`config.json` is present. If population snapshots exist but CSV/Müller outputs do
not, Results marks the run as needing post-processing and can start
`CellEvoX --analyze <run_dir>` for that run from the detail panel.

The web Run Queue intentionally exposes `exports` and `full`, not raw `none`.
`exports` skips heavy plots and animations while keeping durable Results artifacts.
The web API also rejects runs whose final tau is below the configured statistics
or population snapshot resolution, because those runs would not have enough data
for the Results charts. Both resolution fields are expressed in integer `T`
units, not raw loop steps; for `steps=7000` and `tau_step=0.005`, final `T`
is `35`.

Manifest shape:

```json
{
  "runs": [
    { "name": "set 1", "path": "param_set_1_stochastic.json" },
    { "name": "set 2", "config": { "simulation_mode": "stochastic" } }
  ]
}
```

The CLI also accepts a JSON array of config objects.

## Frontend

The Run page has a Batch Queue. It can:

- add the current configured simulation,
- import one or more JSON files,
- import a manifest with a `runs` array,
- launch the queue through `/api/simulation/batch/start`.

The queue is stored in the browser so moving between Configure and Run does not discard queued snapshots.

Runs default to sequential execution. The backend and console runner also support controlled parallelism via `max_parallel` / `--parallel`, with per-process TBB limits to avoid oversubscribing the machine.

## Backend details

Relative `output_path` values are resolved against the repository root by the C++ process. The backend now passes an absolute launch config path to avoid mismatches between the backend working directory and the C++ working directory.
