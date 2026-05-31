# Simulation config fields by mode

This document summarizes the simulation config fields used by the web UI, TypeScript types, FastAPI schema, and C++ parser/engines.

Related docs: [docs index](README.md), [simulation engines](simulation-engines.md), [architecture](architecture.md).

Sources checked:

- `web/frontend/src/components/config/*`
- `web/frontend/src/types/simulation.ts`
- `web/frontend/src/pages/ConfigPage.tsx`
- `web/backend/main.py`
- `web/backend/runner.py`
- `CellEvoX/include/utils/SimulationConfig.hpp`
- `CellEvoX/include/systems/SimulationEngine.hpp`
- `CellEvoX/include/systems/CommonPopulationStep.hpp`
- `CellEvoX/src/core/application.cpp`
- `CellEvoX/src/systems/SimulationEngine.cpp`
- `CellEvoX/src/systems/SimulationEngine3D.cpp`
- `CellEvoX/src/systems/SimulationEngine3DCapacity.cpp`

## Mode model

Supported mode strings in the current web/backend schema are:

- `stochastic`
- `deterministic`
- `spatial_3d_density`
- `spatial_3d_capacity`

`web/frontend/src/types/simulation.ts` also includes `spatial_3d` as a legacy results/config alias. It is not offered by the UI or listed by the backend schema, but the C++ parser accepts it as `spatial_3d_density` for backwards compatibility.

`simulation_mode` is the public source of truth for new web configs:

- `stochastic` -> `STOCHASTIC_TAU_LEAP`
- `deterministic` -> `DETERMINISTIC_RK4`
- `spatial_3d_density` -> `SPATIAL_3D_DENSITY`
- `spatial_3d_capacity` -> `SPATIAL_3D_CAPACITY`
- `spatial_3d` -> `SPATIAL_3D_DENSITY` legacy alias

If `simulation_mode` is absent, the C++ parser still accepts the legacy boolean `stochastic` field:

- `stochastic: true` -> `STOCHASTIC_TAU_LEAP`
- `stochastic: false` -> `DETERMINISTIC_RK4`

The frontend no longer emits `stochastic` in newly exported or launched configs. Imports of old JSON files still infer `simulation_mode` from `stochastic` when needed.

## Field table

Required means required by the C++ parser via `j.at(...)` unless noted otherwise. The web defaults include all fields in `DEFAULT_CONFIG`, and the backend schema describes defaults but does not validate the posted config beyond accepting a `dict`.

| Field | Type | Modes where it makes sense | Required | Legacy/deprecated | Notes |
| --- | --- | --- | --- | --- | --- |
| `simulation_mode` | enum string | All current UI modes: `stochastic`, `deterministic`, `spatial_3d_density`, `spatial_3d_capacity` | Yes for new configs | No | Preferred and canonical mode selector. C++ directly maps all four current values. |
| `stochastic` | boolean | Legacy configs only | No | Legacy/deprecated | Accepted only when `simulation_mode` is absent. New frontend configs do not emit it. |
| `seed` | integer / `uint32_t` | All modes | No | No | Defaults to `42` in C++ if omitted. Used for RNG in stochastic and spatial engines. |
| `tau_step` | float / double | All modes with an implemented step | Yes | No | Used as the simulation time step. Spatial density casts it to float internally. |
| `initial_population` | integer / `size_t` | All modes | Yes | No | Initial number of cells. Also drives initial 3D placement in both spatial engines. |
| `env_capacity` | integer / `size_t` | `stochastic`, `spatial_3d_capacity`; parsed/logged for all modes | Yes | No | Used by `applyCommonPopulationStep` as global carrying capacity. In `spatial_3d_density`, population regulation uses local density instead; `env_capacity` is still required by parser/config but not used by the density step logic found in `SimulationEngine3D.cpp`. |
| `steps` | integer / `size_t` | All modes | Yes | No | Parser stores it, and `Application` separately calls `run(config.at("steps"))`. |
| `max_population_cutoff` | integer / `size_t` | All implemented run loops | No | No | Defaults to `0` in C++ if omitted. `0` disables early stop. Checked in stochastic and both spatial run loops. |
| `output_path` | string | All modes | Yes, unless backend runner injects it before C++ | No | Backend runner sets `./output_<timestamp>` if missing or empty, creates the directory, and writes `config.json`. |
| `statistics_resolution` | integer / `uint32_t` | All implemented engines | Yes | No | Stored as `stat_res`; controls stats snapshots and memory logging. Some engines guard memory logging with `stat_res > 0`; the 2D stochastic code does not guard modulo by zero. UI/backend minimum is `1`. |
| `population_statistics_res` | integer / `uint32_t` | All implemented engines | Yes | No | Stored as `popul_res`; controls population snapshots. UI/backend minimum is `1`. |
| `graveyard_pruning_interval` | integer | All implemented engines | No | No | Defaults to `0` in C++; `0` disables pruning. |
| `full_mutation_payload` | boolean | All modes with population snapshots | No | No | Defaults to `false` in C++. Controls whether snapshots include full mutation payloads. |
| `snapshot_full_mutation_payload` | boolean | All modes with population snapshots | No | Legacy alias | Accepted by C++ parser only if `full_mutation_payload` is absent. Not present in current frontend type/default/backend schema. |
| `verbosity` | enum/integer `0`, `1`, `2` | All modes | No | No | Defaults to `2` in C++ if omitted; frontend/backend default is `2` (`Full`). |
| `phylogeny_num_cells_sampling` | integer / `uint32_t` | Post-run phylogeny/export pipeline; independent of simulation mode | No | No | Defaults to `100` in C++. Exposed in Output UI and backend schema. |
| `mutations` | array of mutation objects | All implemented simulation modes | Yes | No | Parser requires the array with `j.at("mutations")`. Empty arrays are accepted structurally, but the UI warns that at least one mutation is needed for a meaningful simulation. |
| `mutations[].id` | integer / `uint8_t` in `MutationType` usage | All implemented simulation modes | Yes | No | Used as mutation type id. UI increments from current max id. |
| `mutations[].is_driver` | boolean | All implemented simulation modes; result visualization also uses it | Yes | No | Used for driver/passenger labeling and payload filtering/visualization. |
| `mutations[].effect` | float | All implemented simulation modes | Yes | No | Fitness delta. Frontend/backend ranges differ slightly for probability only; effect range is `-0.5..0.5` in both. |
| `mutations[].probability` | float | All implemented simulation modes | Yes | No | Per-cell mutation probability in UI hints. Frontend slider min is `0.00001`; backend schema min is `0.0001`. |
| `spatial_domain_size` | float | `spatial_3d_density`, `spatial_3d_capacity` | No | No | Defaults to `200.0f` in C++. Used to size/clamp the 3D domain and initialize spatial grid/positions. Frontend preview strips it for non-spatial modes. |
| `max_local_density` | float | `spatial_3d_density` | No | No | Defaults to `8.0f` in C++. In density mode, local neighbor count divided by this value controls crowding-dependent death/birth rates. It is not used by `SimulationEngine3DCapacity`, so the web UI/payload omit it for `spatial_3d_capacity`. |
| `sample_radius` | float | `spatial_3d_density` | No | No | Defaults to `3.0f` in C++. In density mode, sets the radius used to count local neighbors for density regulation. It is not used by `SimulationEngine3DCapacity`, so the web UI/payload omit it for `spatial_3d_capacity`. |
| `spring_constant` | float | `spatial_3d_density`, `spatial_3d_capacity` | No | No | Defaults to `0.5f`. Used by mechanical relaxation in both spatial engines. |
| `mech_dt` | float | `spatial_3d_density`, `spatial_3d_capacity` | No | No | Defaults to `0.1f`. Used by mechanical relaxation in both spatial engines. |
| `mech_substeps` | integer | `spatial_3d_density`, `spatial_3d_capacity` | No | No | Defaults to `5`. Mechanical relaxation returns early when `<= 0`, but UI/backend minimum is `1`. |
| `epsilon` | float | `spatial_3d_density`, `spatial_3d_capacity` | No | No | Defaults to `0.1f`. Controls daughter-cell placement jitter/offset during division in both spatial engines. |

## Spatial density vs spatial capacity

`spatial_3d_density` uses `SimulationEngine3D`.

- Birth/death are computed inside `SimulationEngine3D::stochasticStep3D`.
- Local crowding is based on neighbor count within `sample_radius`.
- `max_local_density` scales the crowding ratio.
- `env_capacity` remains required by the parser, but this density step does not use the global capacity calculation from `applyCommonPopulationStep`.
- Mechanical relaxation uses `spring_constant`, `mech_dt`, `mech_substeps`, `epsilon`, and `spatial_domain_size`.

`spatial_3d_capacity` uses `SimulationEngine3DCapacity`.

- Birth/death reuse `applyCommonPopulationStep`, the same global capacity logic used by the 2D stochastic engine.
- `env_capacity` is therefore meaningful as the global carrying capacity.
- Spatial behavior is added after birth/death: assign new 3D positions, rebuild spatial state, run mechanical relaxation.
- Required spatial controls for meaningful capacity-mode behavior are `spatial_domain_size`, `spring_constant`, `mech_dt`, `mech_substeps`, and `epsilon`.
- `max_local_density` and `sample_radius` belong to density mode and are omitted from the capacity UI/export/launch payload.
- No additional capacity-only fields were found beyond common population/output/mutation fields and the spatial mechanics fields above.

## Deterministic mode status

`deterministic` is offered by the frontend and backend schema. The C++ parser can produce `SimulationType::DETERMINISTIC_RK4` from `simulation_mode: "deterministic"` or legacy `stochastic: false`.

Current status: the inspected `SimulationEngine::step()` only handles `STOCHASTIC_TAU_LEAP`; the deterministic case is commented out. This means `deterministic` appears selectable/configurable but is not implemented in the current engine path.

## Open questions

- Should `spatial_3d` remain as a legacy alias, or should old result configs be migrated to `spatial_3d_density`?
- Should backend schema validation enforce the same ranges as the frontend, especially `mutations[].probability` min (`0.00001` frontend vs `0.0001` backend schema)?
- Should UI disable or explicitly mark `deterministic` as unavailable until the C++ `DETERMINISTIC_RK4` step is implemented?
