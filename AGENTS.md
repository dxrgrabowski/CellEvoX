# AGENTS.md

CellEvoX simulates clonal population dynamics under mutation and selection. The
deliverable is statistically valid, reproducible lineage data — not frame rate,
not feature count. Code is the single source of truth for *what* and *how*. This
file records only what cannot be reverse-engineered: domain rules, guardrails,
and external constraints. Command details live in [docs/runbook.md](docs/runbook.md).

## Live simulation modes

There are exactly two supported modes, and the long-term model is
**stochastic base + xyz spatial overlay**:

1. **2D stochastic tau-leap** — density-dependent birth/death/mutation.
2. **3D capacity** — the same stochastic event model with a spatial layer
   (positions, spatial hash grid, soft-sphere mechanical relaxation).

`DETERMINISTIC_RK4` is **dead, deprecated legacy surface**. It is not a
supported mode, must not be advertised as working, and must not be silently
accepted as one. Its remaining parser/UI/code remnants are scheduled for removal
(#48). Re-introducing RK4 would require an explicit, separate decision.

## Hard Domain Invariants

Never violate these, even if the code appears not to enforce them.

1. **`env_capacity` (`Nc`) is a soft baseline, never a ceiling.** It only scales
   density-dependent mortality (`N/Nc`). A lineage with strong driver mutations
   can exceed `Nc` many-fold; there is no upper bound. Never clamp `N` to `Nc`,
   never validate `N <= Nc`, and never size buffers or reserve memory assuming
   `N` stays near `Nc`.
2. **Reproducibility is thread-count-independent.** Randomness comes from a
   counter-based hash keyed by `(seed, step, cell_id, stream)`, not from a shared
   sequential engine. Any change that makes results depend on TBB scheduling,
   iteration order, or thread count is a correctness bug. Stream numbering is
   part of the contract.
3. **Birth replaces the parent.** A birth event kills the parent and installs
   exactly two daughters. `Cell::parent_id` records the parent; mutation payload
   ids record the cell where each mutation originated (inherited ids survive, a
   new mutation's placeholder is rewritten to the daughter's own id). Ordering of
   daughters is fully deterministic (explicit tie-break chain).
4. **2D and 3D must stay semantically identical** for the shared event model.
   They currently carry two copies of the event loop; parity is an invariant
   (see ADR-0003). A change to one loop requires the same change in the other,
   plus a parity check.
5. **3D is strictly `stochastic + xyz`.** Spatial state may not change event
   probabilities, selection, or lineage semantics. Mechanical relaxation only
   resolves overlap; it must not create or destroy cells.
6. **Population snapshots are a public binary ABI** (`CELXPOP1`, v2, 48-byte
   header, 36-byte record, 5-byte mutation payload). The C++ reader also accepts
   v1 and the legacy raw 3D fallback; `snapshot_io.py` requires v2; the web
   backend consumes population CSV, not the binary ABI. Changes must be additive
   and synchronized across C++, `snapshot_io.py`, tests, and the CSV export that
   the web parser reads.
7. **Run output names and CSV columns are a public contract** for the Python
   scripts, `results_parser.py`, and the frontend. Treat renames as breaking.
8. **Snapshot cadence uses integer generation indices**, taken as
   `floor(tau + epsilon)`; integer indexing exists to avoid float-modulo drift.
   Snapshots are written only when that index is a multiple of the configured
   resolution and there is no forced terminal write, so the last snapshot can
   predate the end of the run. Terminal state comes from `run_metadata.json`
   (`terminal_time`, `terminal_population`) and `phylogenetic_tree.csv`.

## Negative Space — Do Not Change Without an ADR

- The duplicated event loop in `SimulationEngine::stochasticDenseStep()` versus
  `applyCommonPopulationStep()`. It is a known debt, not an invitation to delete
  one side ad hoc; unification is the intended direction, but only with parity
  evidence.
- RNG stream ids (`0/1/2`), id assignment order, and daughter tie-break ordering.
- Dense-array storage versus `CellMap`/`Graveyard` materialization boundary.
- Snapshot struct layouts and their `static_assert`s.
- The mechanical relaxation scheme in 3D (soft-sphere springs, per-substep grid
  rebuild). The scheme is protected; its numeric parameters are tuning constants
  covered under Known Tech Debt.
- The web runner's single-global-job model and atomic launch-config write.
- `CELLEVOX_NATIVE_OPTIMIZATIONS` defaulting ON for local/benchmark builds.

## Known Tech Debt / Cleanup Targets

Safe to fix when scoped, benchmarked, and covered by tests; no ADR is required
for these. Do not preserve them as architecture.

- **Duplicated 2D event loop** introduced by the dense-array optimization
  (904f5dd); the separately removed 3D density mode (36013fa) is unrelated.
  Long-term goal is one shared population step.
- **Dead `rng` parameter** in `applyCommonPopulationStep` (vestige of the
  pre-hash-RNG era).
- **Arbitrary tuning constants** (sort threshold, small-batch threshold, rebuild
  chunk, cache slack, dense-voxel cap, web log throttles). They are rough
  engineering defaults, not invariants; adjust with benchmark evidence.
- **Hardcoded DevContainer paths** (`/workspaces/CellEvoX/...`) in
  `RunDataEngine.cpp` and `test_config.json`. There must be no hardcoded
  devcontainer paths in C++; outputs/scripts should be relative or configured.
- **Hardcoded `0.005` tau** in `Application`'s analysis path instead of
  `config.tau_step` (oversight from research defaults).
- **Dead `log_level` config key** (never parsed).
- **`--postprocess csv` alias** accepted by the CLI/console runner but rejected
  by the web API and undocumented in `--help`.
- **Dead deterministic mode** in the parser/web schema.
- **Qt, QML, Boost, and `-DSKIP_GUI=ON`** are fully removed from the project.
  Remaining references in CI are leftovers from old baseline comparisons; remove
  them once the benchmark baseline commit is updated.

## External Traps & Boundaries

- **Portability:** `-march=native` is ON by default. Portable Docker images and
  binaries for other hosts MUST configure `-DCELLEVOX_NATIVE_OPTIMIZATIONS=OFF`,
  otherwise `SIGILL` is possible on different CPUs.
- **Build-time dependencies:** Python 3 *development headers* and NumPy are
  required at CMake configure time (the simulator embeds Python for plotting).
  There is no Qt dependency.
- **Runtime dependencies:** Python `matplotlib`/`numpy`/`pandas`/`networkx` and
  `pyfish` for post-processing (`CellEvoX/scripts/requirements-visualization.txt`);
  `ffmpeg` for MP4 (GIF fallback otherwise); PyVista optional for 3D replay. Set
  `MPLCONFIGDIR` in headless containers.
- **Thread limits:** `CELLEVOX_TBB_THREADS` and `--threads` cap TBB inside a
  process. For batch runs keep `parallel × threads-per-run` near the core count;
  oversubscription dominates gains.
- **Web layer:** backend `7432`, frontend dev server `5274`, CORS allowlist is
  fixed; one simulation job at a time (HTTP 409 otherwise).
- **Memory reporting** reads `/proc/self/statm` on Linux; treat `memory_log.csv`
  as Linux-oriented and avoid frequent flushes in performance runs.

## Documentation Rules

- Do not duplicate what code already shows (directory trees, signatures, config
  field tables, control flow); stale copies cause confident wrong turns. Public
  contracts that outside consumers depend on — run outputs and CSV columns — are
  the exception and live in [docs/output-contract.md](docs/output-contract.md).
- Add *why* to `docs/adr/` when you make a non-obvious decision.
- For descriptive documentation, code wins: fix the doc in the same change. For
  approved domain invariants (above), the invariant wins: do not weaken it to
  match the code — report the discrepancy, then fix the code or request a domain
  decision.
