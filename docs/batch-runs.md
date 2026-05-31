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

Runs are executed sequentially for now. That keeps output, logs, and process control predictable while still letting us add controlled parallelism later.

## Backend details

Relative `output_path` values are resolved against the repository root by the C++ process. The backend now passes an absolute launch config path to avoid mismatches between the backend working directory and the C++ working directory.
