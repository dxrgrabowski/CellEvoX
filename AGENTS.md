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
in each entry is how the PR description should name the invariant; it does not
authorize a redefinition. Changing an invariant deliberately needs the owner
decision and Accepted ADR described under Precedence.

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

- Write repository text in English: code, comments, commit messages,
  documentation, and PR bodies. Quote the owner's request translated or
  summarized, not verbatim in another language.
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
- Documentation-only changes state the intended documentation outcome and the
  checks that apply; runtime tests may be unnecessary.

## Change Workflow

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
- **Name the requirement and the success condition.** Quote the task, issue, or
  comment that asked for the change; for a conversational request, quote or
  summarize the sentence that carries the requirement, and do not demand a link
  when none exists. Separate what the owner asked for from what you inferred.
  When the request implies a pass/fail condition, state it in `Goal and result`,
  including what must keep working; a larger change may add an `Acceptance
  criteria` subsection. Derive a criterion only when it follows unambiguously
  from the request, and present it as derived. Ask the owner only where the
  request is genuinely ambiguous or the answer chooses new domain semantics.
  An analysis-only request stops at analysis until the owner says to implement.
  Never write a criterion after the fact to justify code that already exists,
  and never invent precision the owner has not agreed to.
- **The PR body is the handoff between agents.** Answer the sections of
  `.github/pull_request_template.md` that apply and delete the rest, including
  their comments; scale the description to the change instead of filling sections
  for their own sake. No parser checks the structure — the reviewer decides
  whether the context is sufficient. State the task context a reviewer cannot
  recover from the diff. Declared behavioral changes are intentional;
  everything the description does not declare is judged against the previous
  behavior, the tests, and the invariants. Reasoning belongs in the PR body or
  in an ADR — not in a new descriptive file under `docs/`.
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
- **A review round does not rewrite the target silently.** The description
  carries current values, not a history. Changing the intended behavior, an
  acceptance criterion, or an agreed constraint needs a short note in the
  description: what changed, why, and which agreement it rests on. Never quietly
  narrow a criterion or a constraint to fit a later commit.
- **When the task includes delivering a PR, ready for review means** the build
  is clean and the tests for the touched code pass, locally or in CI with the
  run linked, and `Verification` says what was checked, against which commit,
  with the decisive output, and what was not checked. Before asking for review,
  self-review the whole diff and verify the checks relevant to the change,
  locally or through linked CI results. Add or update tests when they provide
  meaningful regression coverage. Explain any important verification gaps. Full
  or noisy logs live in an accessible CI run or uploaded artifact linked from
  `Verification`; the verdict and the lines it rests on stay in the body. A
  criterion with no evidence is an unverified claim, not a pass. Opening a draft
  is how a change reaches CI, so the requirement binds when the task is done and
  the PR is ready for review.
- **A regression is fixed, not absorbed.** Benchmark thresholds are engineering
  defaults, not invariants: correcting one needs paired base/head measurements on
  the same host, recorded in the PR
  ([ADR-0008](docs/adr/0008-performance-gate.md)). Raising a threshold so a
  measured regression passes is never a fix, and no ADR is needed for the
  correction itself.

## Code Review Rules

Review the change, not the author's summary of it.

- **Read the complete diff and every affected consumer.** A declared intent
  waives neither correctness, nor compatibility, security, nor any Hard Domain
  Invariant. Check the callers, formats, docs, and tests the diff touches or
  invalidates.
- **Verify every applicable invariant**, named in the description or not. A Hard
  Domain Invariant or Negative Space rule broken without a `Status: Accepted`
  ADR in the same diff is a finding.
- **Compare the result with the accepted requirements.** Flag any gap between the
  implementation, the description, and what the owner asked for, including a
  criterion added after the fact.
- **Judge the evidence.** It must match the commit under review and support each
  criterion; a missing check must be acknowledged, not implied. An author's risk
  estimate is not a review setting: assess risk independently, and raise the
  evidence bar rather than lower the scope for a higher-risk change.
