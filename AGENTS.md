# AGENTS.md

CellEvoX simulates clonal population dynamics under mutation and selection. The
deliverable is statistically valid, reproducible lineage data — not frame rate,
not feature count. Code is the single source of truth for *what* and *how*. This
file records only what cannot be reverse-engineered: domain rules, guardrails,
and external constraints. Command details live in [docs/runbook.md](docs/runbook.md).

## Precedence

Invariants > code > descriptive docs. For descriptive documentation, code wins —
fix the doc in the same change. For Hard Domain Invariants and Negative Space,
the invariant wins by default: never weaken it to match broken code. Changing an
invariant, or superseding an ADR, requires an explicit domain-owner decision and
a `Status: Accepted` ADR in the same PR; the old ADR is marked
`Superseded by ADR-XXXX`, never deleted. An agent may argue that an invariant or
ADR is wrong — the argument goes in the PR body or the new ADR, and the change
waits for the owner.

## Live simulation modes

There are exactly two supported modes, built on
**stochastic base + xyz spatial overlay**:

1. **Non-spatial stochastic tau-leap** — density-dependent birth/death/mutation.
2. **3D capacity** — the same stochastic event model with a spatial layer
   (positions, spatial hash grid, soft-sphere mechanical relaxation).

`DETERMINISTIC_RK4` was a dead, deprecated legacy simulation mode. It is not a
supported mode, must not be advertised as working, and must not be silently
accepted as one. Its parser/UI/code remnants were removed in #48; configs that
request it now fail fast. Re-introducing RK4 would require an explicit, separate
decision.

## Hard Domain Invariants

Never violate these, even if the code appears not to enforce them. The bold slug
in each entry is how the PR description should name the invariant. Naming an
invariant means the reviewer must verify the diff still preserves it; it does not
authorize a redefinition. Changing an invariant deliberately needs
the owner decision and Accepted ADR described under Precedence.

1. **`env-capacity`** — `env_capacity` (`Nc`) is a soft baseline, never a
   ceiling. It only scales density-dependent mortality (`N/Nc`). A lineage with
   strong driver mutations can exceed `Nc` many-fold; there is no upper bound.
   Never clamp `N` to `Nc`, never validate `N <= Nc`, and never size buffers or
   reserve memory assuming `N` stays near `Nc`.
2. **`determinism`** — Reproducibility is thread-count-independent. Randomness
   comes from a counter-based hash keyed by `(seed, step, cell_id, stream)`, not
   from a shared sequential engine. Any change that makes results depend on TBB
   scheduling, iteration order, or thread count is a correctness bug. Stream
   numbering is part of the contract.
3. **`birth-replaces-parent`** — A birth event kills the parent and installs
   exactly two daughters. `Cell::parent_id` records the parent; mutation payload
   ids record the cell where each mutation originated (inherited ids survive, a
   new mutation's placeholder is rewritten to the daughter's own id). Ordering of
   daughters is fully deterministic (explicit tie-break chain).
4. **`mode-parity`** — Base and xyz must stay semantically identical for the
   shared event model. Both use the same dense population step; apply event/RNG
   changes there and keep the parity test green. Preserve dense storage and
   optional event-result collection — see
   [ADR-0003](docs/adr/0003-dense-population-step.md).
5. **`3d-xyz-overlay`** — 3D is strictly `stochastic + xyz`. Spatial state may
   not change event probabilities, selection, or lineage semantics. Mechanical
   relaxation only resolves overlap; it must not create or destroy cells.
6. **`snapshot-abi`** — Population snapshots have one binary ABI per version.
   Layout, sizes, flags and the reader matrix live in
   [docs/output-contract.md](docs/output-contract.md#binary-snapshot-abi-celxpop1);
   the rule here is that a change to the serialized layout, encoding, or
   interpretation needs a version bump plus synchronized updates to every
   in-repo reader, the tests, and the CSV export. A writer refactor that
   emits the same bytes needs no bump. Compatibility with older
   versions is **not** supported or guaranteed, so old runs need not stay
   readable and the remaining v1/raw-3D snapshot readers are removable legacy.
7. **`output-names`** — Run output names and CSV columns are a contract for the
   Python scripts, `results_parser.py`, and the frontend at the current commit.
   Treat renames as breaking and update every in-repo consumer in the same
   change; old run directories do not have to stay readable.
8. **`snapshot-cadence`** — Snapshot cadence uses integer generation indices,
   taken as `floor(tau + epsilon)`; integer indexing exists to avoid float-modulo
   drift. There is no forced terminal write, so the last snapshot can predate the
   end of the run and terminal state comes from `run_metadata.json` and the
   phylogeny. Details in
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

- **Hardcoded `0.005` tau** in `Application`'s analysis path instead of
  `config.tau_step` (oversight from research defaults).

## Documentation Rules

- Do not duplicate what code already shows line by line (directory trees,
  signatures, control flow); stale copies cause confident wrong turns. Public
  surfaces that outside consumers depend on are the exception and need durable
  docs: run outputs and file formats in
  [docs/output-contract.md](docs/output-contract.md), and the CLI, configuration
  keys with a short runnable example, and the HTTP API in
  [docs/runbook.md](docs/runbook.md). Inputs are as much a contract as outputs.
- Each topic has one home: this file for invariants and guardrails,
  [docs/runbook.md](docs/runbook.md) for commands, dependencies, ports,
  environment facts and public input surfaces, `docs/output-contract.md` for
  output formats, and `docs/adr/` for the rationale and consequences of a
  decision. An ADR may quote a benchmark result or a key flag as evidence, but
  not full run procedures or dependency inventories.
- Add *why* to `docs/adr/` when you make a non-obvious decision.
- Agents may propose an ADR. If a task forces a non-obvious trade-off (a new
  dependency, a format change, a concurrency model), create
  `docs/adr/NNNN-slug.md` from the template in `docs/adr/README.md`. File it as
  `Status: Proposed` and let the domain owner accept or reject it — unless the
  owner explicitly authorized that decision during the working session, in which
  case file it as `Status: Accepted` with `Evidence: owner decision, YYYY-MM-DD`.
  Do not silently pick a design that deserves a record, and do not open an ADR
  for a reversible local choice: an ADR exists to constrain future work, so state
  what it forbids.

## Change Workflow

One PR carries one coherent change. A reviewer holds the whole PR as a single
context, so unrelated work bundled together hides the risky part behind the
noisy one and makes a revert take everything down with it. Each PR states one
intent, touches only the files that intent needs, and can be merged or rejected
on its own.

- **One coherent outcome per PR.** Implementation, tests, docs, and caller
  updates for that outcome belong together; two unrelated outcomes are two PRs —
  a title joined by "and" is a hint, not a rule. A shared file (workflow, config,
  template) belongs to the PR whose change is the reason it changes.
- **Opportunistic finds need owner approval.** Do not bundle unrequested
  refactoring or "while I'm here" cleanups. A repair the outcome cannot be
  completed without — a caller left stale by an interface change, a test helper
  that no longer compiles — is part of that outcome: make the minimal fix and
  declare it in the description. Everything else found in adjacent code, a
  critical bug included, is reported for the owner to place in this PR or a
  follow-up, and listed in the out-of-scope notes when it stays out. Independent
  cleanups, product extensions, and new domain semantics are decisions rather
  than repairs, and never ride along.
- **State the real boundary in the description.** Say what the change leaves
  alone and which properties must keep working when the task does not make that
  obvious. Do not invent limits, empty lists, or a file map: the diff already
  shows the files, and describing them back checks nothing. A listed constraint
  guides review but does not displace the user's requirements or AGENTS.md.
  Stage files explicitly (`git add <path>`, never `git add .`), so worktree
  directories, build output, and benchmark or test artifacts stay out of the
  commit.
- **Acceptance criteria are agreed before the change is finalized.** State the
  observable pass/fail condition, including what must keep working. A change with
  no observable behavior — documentation or a comment — says so instead of
  inventing a criterion. When the request does not spell one out, derive it only
  when it follows unambiguously from the request, and present it as derived
  rather than as the user's words. Ask the owner only where the request is
  genuinely ambiguous, or where the answer chooses new domain semantics. If the
  owner asked for analysis only, stop there; implementation starts on an explicit
  go-ahead. Never write a criterion after the fact to justify code that already
  exists, and never invent precision the owner has not agreed to.
- **Quote the requirement, not a label for it.** Name the task, issue, or comment
  that asked for the change; when it came from a conversation, quote or summarize
  the sentence that carries the requirement. Separate what the user asked for
  from what you inferred. Do not demand a link when none exists, and do not ask
  for confirmation of an implication the task makes obvious.
- **The PR body is the handoff between agents.** Answer every applicable section
  of `.github/pull_request_template.md` and delete its scaffolding; drop a section
  that does not apply instead of writing "none". The automated check reads
  headings and the presence of an answer; it cannot tell whether the answer is
  true, whether the user approved it, or whether the change works.
  Declared behavioral changes are intentional; everything the description does
  not declare is judged against the previous behavior, the tests, and the
  invariants. Reasoning belongs in the PR body or in an ADR — not in a new
  descriptive file under `docs/`.
- **Explain only the choice a reviewer would otherwise re-litigate.** When the
  diff takes a non-obvious route — a new dependency, a format, an architectural
  trade-off — name what you ruled out and the constraint that ruled it out. An
  obvious fix needs no invented alternatives. A rejected option is context for
  the reviewer, not a claim that the chosen one is right.
- **Keep decisions and open questions visible.** Say what you decided within your
  mandate, what you proposed to the user, what the user explicitly approved, what
  you told the user but have no answer to, and what is still open. Silence, or a
  general "open the PR and merge", is not approval. Raise a significant
  inconsistency as soon as you find it, before finalizing the change: state its
  impact, the possible resolutions, and your recommendation so the user can
  answer quickly. Routine choices and repairs the agreed task needs are decided
  without asking; an open side issue does not block work automatically, but a
  correctness problem in the change and any decision beyond your mandate are
  resolved before merge. Record the outcome, not a table of statuses, dates, or
  empty fields.
- **Intent declares the change; it does not bound the review.** Read the complete
  diff and the callers, consumers, and formats it affects. A declaration waives
  neither correctness, nor compatibility, security, nor any Hard Domain
  Invariant; verify every invariant the diff could break, listed or not. An
  author's risk estimate is not a review setting: the reviewer assesses risk
  independently and reads the whole diff, affected consumers, and applicable
  invariants whatever the description says. Higher risk raises the evidence bar —
  preserved-behavior criteria, owner sign-off, invariant/ADR analysis — not the
  minimum scope of the review. Flag any gap between the implementation, the
  description, and the accepted requirements — including an invariant broken
  without a `Status: Accepted` ADR in the same diff.
- **A review round does not rewrite the target silently.** The description
  carries current values, not a history. Changing the intended behavior, an
  acceptance criterion, or an agreed constraint needs a short note under
  Acceptance criteria: what changed, why, and which agreement it rests on. The
  updated criterion needs renewed review. Updating the implementation
  description or the evidence is an ordinary PR update. Never quietly narrow a
  criterion or a constraint to fit a later commit.
- **Done means a mergeable PR.** Opening a draft is how a change reaches CI: a
  draft PR may open before the checks have run. The requirement binds when the
  task is done and the PR is ready for review — by then the build is clean and the
  tests for the touched code pass, locally or in CI with the run linked, and
  `Verification` says what was checked, against which commit, with the decisive
  output, and what was not checked. Full or noisy logs live in an accessible CI
  run or uploaded artifact linked from that section; the verdict and the lines it
  rests on stay in the body. A criterion with no evidence is an unverified claim,
  not a pass. A complete description does not prove the change is correct: the
  reviewer still judges whether the criteria match the requirements, whether the
  checks are adequate, and whether the evidence is current.
- **A regression is fixed, not absorbed.** Benchmark thresholds are engineering
  defaults, not invariants: correcting one needs paired base/head measurements on
  the same host, recorded in the PR
  ([ADR-0008](docs/adr/0008-performance-gate.md)). Raising a threshold so a
  measured regression passes is never a fix, and no ADR is needed for the
  correction itself.
