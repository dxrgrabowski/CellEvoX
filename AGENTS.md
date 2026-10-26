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
   They currently carry two copies of the event loop: apply every event/RNG
   change to both and keep the parity test green, but do not unify or delete
   either copy ad hoc — see [ADR-0003](docs/adr/0003-dual-storage-engines.md).
5. **3D is strictly `stochastic + xyz`.** Spatial state may not change event
   probabilities, selection, or lineage semantics. Mechanical relaxation only
   resolves overlap; it must not create or destroy cells.
6. **Population snapshots have one binary ABI per version.** Layout, sizes,
   flags and the reader matrix live in
   [docs/output-contract.md](docs/output-contract.md#binary-snapshot-abi-celxpop1);
   the rule here is that a writer change needs a version bump plus synchronized
   updates to every in-repo reader, the tests, and the CSV export. Compatibility
   with older versions is **not** supported or guaranteed, so old runs need not
   stay readable and the remaining v1/raw-3D readers are removable legacy.
7. **Run output names and CSV columns are a contract** for the Python scripts,
   `results_parser.py`, and the frontend at the current commit. Treat renames as
   breaking and update every in-repo consumer in the same change; old run
   directories do not have to stay readable.
8. **Snapshot cadence uses integer generation indices**, taken as
   `floor(tau + epsilon)`; integer indexing exists to avoid float-modulo drift.
   There is no forced terminal write, so the last snapshot can predate the end of
   the run and terminal state comes from `run_metadata.json` and the phylogeny.
   Details in
   [docs/output-contract.md](docs/output-contract.md#snapshot-cadence-and-terminal-state).

## Negative Space — Do Not Change Without an ADR

- RNG stream ids (`0/1/2`), id assignment order, and daughter tie-break ordering.
- Dense-array storage versus `CellMap`/`Graveyard` materialization boundary.
- Snapshot struct layouts and their `static_assert`s.
- The mechanical relaxation scheme in 3D (soft-sphere springs, per-substep grid
  rebuild); its numeric parameters are tuning constants, not invariants.
- The web runner's single-global-job model and atomic launch-config write.
- `CELLEVOX_NATIVE_OPTIMIZATIONS` defaulting ON for local/benchmark builds.
  Portable Docker images and binaries for other hosts must configure
  `-DCELLEVOX_NATIVE_OPTIMIZATIONS=OFF`, otherwise `SIGILL` is possible on a
  different CPU ([ADR-0007](docs/adr/0007-build-portability.md)).

## Known Tech Debt

Safe to fix when scoped and covered by tests; no ADR is required. Do not preserve
these as architecture. Smaller leftovers — dead config keys, CLI aliases, tuning
constants, stale Qt/Boost references in CI — belong in issues.

- **Hardcoded DevContainer paths** (`/workspaces/CellEvoX/...`) in
  `RunDataEngine.cpp` and `test_config.json`. There must be no hardcoded
  DevContainer paths in C++; outputs and scripts should be relative or configured.
- **Hardcoded `0.005` tau** in `Application`'s analysis path instead of
  `config.tau_step` (oversight from research defaults).

## Documentation Rules

- Do not duplicate what code already shows (directory trees, signatures, config
  field tables, control flow); stale copies cause confident wrong turns. Public
  contracts that outside consumers depend on — run outputs and CSV columns — are
  the exception and live in [docs/output-contract.md](docs/output-contract.md).
- Each topic has one home: this file for invariants and guardrails,
  [docs/runbook.md](docs/runbook.md) for commands, dependencies, ports and
  environment facts, `docs/output-contract.md` for output formats, and
  `docs/adr/` for the rationale and consequences of a decision. An ADR may quote
  a benchmark result or a key flag as evidence, but not full run procedures or
  dependency inventories.
- Add *why* to `docs/adr/` when you make a non-obvious decision.
- Agents may propose an ADR. If a task forces a non-obvious trade-off (a new
  dependency, a format change, a concurrency model), create
  `docs/adr/NNNN-slug.md` from the template in `docs/adr/README.md` with
  `Status: Proposed`, link it from the change description, and let the domain
  owner accept or reject it. Do not silently pick a design that deserves a
  record.
- For descriptive documentation, code wins: fix the doc in the same change. For
  approved domain invariants (above), the invariant wins: do not weaken it to
  match the code — report the discrepancy, then fix the code or request a domain
  decision.
