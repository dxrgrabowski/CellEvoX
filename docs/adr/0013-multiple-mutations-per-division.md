# ADR-0013: Poisson-counted multiple mutations per division
Status: Proposed
Date: 2026-11-20

## Context
A division currently carries at most one mutation. The event loop draws a single
`u ∈ [0,1)` from the mutation stream, compares it against
`Σ p_i = total_mutation_probability` (validated to `[0, 1]`), and, on a hit,
picks one type by cumulative sums. Only daughter A receives it; daughter B is a
verbatim copy of the parent.

That model cannot express two things the biology asks for: (a) the number of
mutations per division is a counting process, not a Bernoulli trial, and (b)
two independent mutations can land in the same division. Issue #59 asks for a
Poisson count while keeping every existing run bit-identical, and requires
Bernoulli and Poisson mutation types to coexist in one run.

Constraints that pin the design:

- `determinism` (ADR-0001): results must not depend on thread count, so any new
  randomness must come from counter-based `uniform01(seed, step, cell_id,
  stream)` draws, never from shared state.
- `birth-replaces-parent` (ADR-0002): a birth still installs exactly two
  daughters and still rewrites the `{0, type_id, effect}` placeholder to the
  daughter's id.
- `mode-parity`: the dense 2D loop and the 3D `CellMap` loop both delegate to
  `appendMutatedDaughters`, so the new draw sequence and mutation order must be
  identical for both.
- `snapshot-abi` (ADR-0004): `mutations_count` is a `uint16` and the per-cell
  mutation payload is already variable-length, so k > 1 needs no new ABI fields.
  Version 3 already carries `(mutation_id, mutation_type, effect)`.
- ADR-0012 (merged as #69) owns RNG stream `3` for per-mutation gamma effects,
  adds `kStreamMutationEffect`, and defines
  `substream(base, slot) = base + slot * 2^32`. `MutationEffect.cpp` currently
  passes `mutation_slot = 0` and explicitly reserves slot `1+` "for a future
  second mutation". This ADR fills that reservation and must not reuse stream
  `3`.

## Decision

### Per-mutation-type model
The count model is a property of each mutation type, not of the run, so a
single run can mix both kinds of mutation:

- `count_model: "bernoulli"` (default) — the existing Bernoulli draw against
  `Σ p_i`. Available only to types in the Bernoulli pool.
- `count_model: "poisson"` — a Poisson counting process with rate `lambda_i`.

A run is a *hybrid* when both pools are non-empty. This is the required
configuration; a legacy config (all types Bernoulli) and an all-Poisson config
are just the two extremes of the same mechanism, so no global selector is
needed.

Effect variability is orthogonal and comes from ADR-0012:
`effect_mode: "constant" | "gamma"` per type. Any combination (Bernoulli +
gamma, Poisson + constant, Poisson + gamma, ...) is valid, so variable effects
compose with the Poisson count in the same division.

### Parameters
- Bernoulli types keep `probability`, and only those enter
  `Σ p_i <= 1 + 1e-9`.
- Poisson types use `lambda >= 0` (finite) and contribute `Λ = Σ lambda_i`;
  the Bernoulli bound does not apply and `Λ` is unbounded. There is no hard
  cap on `k`: for realistic `Λ` the Poisson tail is negligible, and a cap would
  need its own clamp-versus-error rule. A typo in `lambda` therefore yields a
  correspondingly large count, not a validation failure.
- Per type exactly one of `probability` / `lambda` must be present, matching
  its `count_model`; the other is rejected rather than ignored.

### Count and assignment
For a birth, in this fixed order:

1. **Bernoulli pool** (skipped entirely when the pool is empty): draw `u` from
   the legacy mutation stream exactly as today; if `u < Σ p_i`, select one type
   by cumulative `p_i` in ascending `type_id` order.
2. **Poisson pool** (skipped entirely when the pool is empty): draw
   `k ~ Poisson(Λ)`. For `j = 0..k-1`, draw the type by weighted selection with
   weights `lambda_i / Λ`, iterating ascending `type_id` and selecting the
   first type whose cumulative share exceeds `u_j · Λ`.
3. The event's mutation list is the Bernoulli mutation (if hit) followed by the
   `k` Poisson mutations, so the Bernoulli mutation always occupies slot `0`.
4. All mutations go to daughter A; daughter B is always an unmutated copy of
   the parent. `k = 0` and no Bernoulli hit means a neutral division with two
   identical daughters, both with fresh ids.
5. Each mutation's placeholder `{0, type_id, effect}` is appended in list order
   and rewritten to A's id after id assignment.
6. Fitness of daughter A is the sequential product in list order:
   `fitness_A = parent_fitness · Π_m (1 + effect_m)`, where `effect_m` comes
   from `realizedMutationEffect(type, seed, step, cell_id, /*slot=*/m)` — the
   configured `effect` for a constant type, the gamma draw for a gamma type.
   Evaluate in increasing `m` so the floating-point result is order-stable;
   every step must stay finite and > 0.

In code this is one function: `appendMutatedDaughters` currently owns the
Bernoulli roulette, the effect sampling and the daughter construction, and both
engines already call it. It grows the pool loop and the `k` loop; `mutation_slot`
stops being hard-coded `0` and becomes the mutation's list index.

### RNG streams
Reuse ADR-0012's `substream(base, slot)` helper and add two named constants next
to `kStreamDeath/Birth/Mutation/MutationEffect`. Streams are counter-based, so
*not* reading a stream shifts nothing else — pool emptiness and `effect_mode`
therefore never perturb other draws.

- `kStreamMutation = 2`: Bernoulli draw `u` — one per birth, only when the
  Bernoulli pool is non-empty. Unchanged for legacy configs.
- `kStreamMutationCount = 1 << 56`: Poisson count,
  `substream(kStreamMutationCount, 0)` — one per birth, only when the Poisson
  pool is non-empty.
- `kStreamMutationType = 1 << 57`: Poisson type selection,
  `substream(kStreamMutationType, j)` — one per Poisson mutation `j`.
- `kStreamMutationEffect = 3` (ADR-0012): gamma effect,
  `substream(3, m)`, where `m` is the mutation's index in the event list; read
  only for gamma-mode types.

The count and type purposes must not use adjacent low ids (`4`/`5`). The gamma
sampler starts at stream `3` and walks forward one key per draw, so a low count
id aliases the effect draws and the type draw aliases the count's second PTRS
uniform, correlating the count with the selected type. Placing both purposes far
above that contiguous range keeps every draw key disjoint.

The low 32 bits of each substream stay the draw index, so the gamma sampler keeps
working per slot exactly as it does for slot `0` today. Because `m` is the
position in the event list (not a count of effect draws), a constant-mode
mutation never consumes a gamma slot. In a legacy config only `0/1/2` are read
(plus `3` for a gamma type), so existing runs stay bit-identical.

### Downstream semantics and output impact
Mutations from one division share `mutation_id` (the origin daughter) but differ
in `mutation_type` and `effect`. `Cell::mutations` is now
`std::vector<CellMutation{mutation_id, mutation_type, effect}>`, so duplicates
are representable, but the consumers were written for "one mutation per cell per
id" and break in four places:

- **VAF inflates.** `RunDataEngine::plotMutationFrequency` increments
  `mutation_counts[mutation_id]` per record, in both the in-memory path and the
  binary-snapshot path. A cell carrying two copies of one origin id then counts
  twice, so a VAF can exceed `1`. It must count the *presence* of a key per
  cell.
- **Clone signatures diverge.** `results_parser._driver_signature` sorts origin
  ids without deduplicating, so a cell with driver mutations `(5,1)` and `(5,2)`
  yields `"5,5"`, while `snapshot_io.driver_signature_from_driver_ids` uses a
  set and yields `"5"`. The same run therefore gets different clone labels
  depending on the reader.
- **Muller parent search is fragile.** `_muller_parent_signature` drops the
  last comma token and assumes ids are monotonic; duplicate tokens make that
  heuristic ambiguous.
- **`mutation_id` alone is not an allele.** Two mutations from one division can
  have different types. Keying VAF/clones by `mutation_id` only merges them.

The structural payload stays valid: `MutationCount` remains the number of
records, `mutations_count` / `driver_mutation_count` remain counts, the flat
`(mutation_id, mutation_type, effect)` payload already allows duplicates, and no
new ABI version is needed beyond v3. Only the *interpretation* has to be fixed.

Recommended output rule:

- an allele is `(mutation_id, mutation_type)`; a cell either carries it or not;
- VAF = cells carrying the allele / total cells (deduplicate per cell);
- `MutationCount` = occurrences, including two hits of the same allele;
- clone signatures and Muller ancestry use the deduplicated allele set;
- `docs/output-contract.md` states that `mutation_id` is not unique within a
  cell and that duplicates are independent occurrences, not a data error.

Clone-signature labels are hybrid, so existing runs keep their labels while
hybrid runs lose nothing:

- deduplicate by the allele key `(mutation_id, mutation_type)` in every reader;
- render a token as the bare `mutation_id` when that id carries exactly one type
  in the cell (the only case that exists in pre-#59 data);
- render it as `mutation_id:mutation_type` only when the same id appears with
  more than one type in that cell;
- both `results_parser._driver_signature` and
  `snapshot_io.driver_signature_from_driver_ids` must implement exactly this
  rule, and `_muller_parent_signature` must drop a whole token, not assume
  every token is a bare integer.

### Configuration sketch
```json
{
  "mutations": [
    { "id": 1, "is_driver": true,  "count_model": "bernoulli",
      "probability": 0.10, "effect_mode": "constant", "effect": 0.10 },
    { "id": 2, "is_driver": true,  "count_model": "poisson",
      "lambda": 0.02, "effect_mode": "gamma",
      "effect_mean": 0.06, "effect_std": 0.03,
      "effect_direction": "positive", "effect_limit": 0.15 },
    { "id": 3, "is_driver": false, "count_model": "poisson",
      "lambda": 0.05, "effect_mode": "constant", "effect": -0.01 }
  ]
}
```
Omitting `count_model` means `"bernoulli"` and keeps the current `probability`
requirement. Omitting `effect_mode` means `"constant"` (ADR-0012). The legacy
shape `{"id", "is_driver", "effect", "probability"}` keeps working unchanged, and
a Poisson type does not carry `probability` at all.

## Alternatives considered
- **Global `mutation_count_model` selector only.** Cannot express the required
  mixed run; rejected in favour of a per-type field that also covers both
  extremes.
- **Binomial count on Σ p_i.** Bounded by 1, so it cannot express k > 1 for
  small per-division windows; Poisson matches the requested model and the
  `λ` scale is unbounded by construction.
- **Split mutations between daughter A and B.** Would break the single-daughter
  lineage rule and complicate VAF interpretation for no requested benefit.
- **Draw k unconditionally per cell per step.** Would change the RNG stream
  usage for non-dividing cells and destroy bit-identical legacy runs.
- **Draw the gamma effect for every mutation regardless of `effect_mode`.**
  Unnecessary; counter-based substreams make skipping safe and keep slot
  indices meaningful.
- **Default to Poisson.** Rejected: it silently changes every existing
  dataset, benchmark, and golden test.

## Consequences
- Configs without `count_model` keep the Bernoulli draw and remain
  bit-identical; hybrid and all-Poisson runs are explicit opt-ins.
- Poisson types require new validation (finite `lambda >= 0`), a Poisson
  sampler built on `uniform01`, and streams `4`/`5` reserved as above.
- Changing stream ids, the pool order (Bernoulli then Poisson), the slot
  indexing, the fitness product order, or the "all-mutations-on-A" rule changes
  results and snapshot contents.
- VAF, `driver_signature`, and any clone definition must be per distinct allele
  `(mutation_id, mutation_type)`, not per record; both the C++ statistics path
  and `web/backend/results_parser.py` are in scope. Clone tokens keep the bare
  id unless one id carries several types in the cell, so pre-#59 outputs are
  unaffected.
- `total_mutations_memory` grows with the average number of mutations per
  division; no new memory accounting is introduced.
- `Σ p_i <= 1` describes only the Bernoulli pool; `Λ` is validated separately.
- ADR-0012's "Multiple mutations per division event remain out of scope" no
  longer holds; ADR-0012's slot-`1+` reservation in `MutationEffect.cpp` is
  consumed by this design and both ADRs must cross-reference each other.
- `MutationEffect.cpp`'s comment ("Slot 0 is the only slot in use") becomes
  obsolete and must be rewritten.

## Implementation surface
Code lands separately from this ADR on the `issue-59-multiple-mutations-per-division`
branch. Touch points:

- `CellEvoX/include/ecs/Cell.hpp` — extend `MutationType` with `count_model`
  and `lambda`; `effect_mode`, `effect_mean`, `effect_std`, `effect_direction`,
  `effect_limit` and `CellMutation{mutation_id, mutation_type, effect}` already
  exist from ADR-0012.
- `CellEvoX/include/utils/SimulationConfig.hpp` — parse `count_model` /
  `lambda`, enforce `probability`/`lambda` exclusivity, sum `Σ p_i` over the
  Bernoulli pool only, compute `Λ`, keep the named-field parsing style it uses
  since ADR-0012.
- `CellEvoX/include/utils/DeterministicRng.hpp` — Poisson sampler on top of
  `uniform01`, plus `kStreamMutationCount` / `kStreamMutationType` high, sparse
  constants next to the existing stream constants.
- `CellEvoX/src/systems/MutationEffect.cpp` and
  `CellEvoX/include/systems/MutationEffect.hpp` — `appendMutatedDaughters`
  grows the two pools and the `k` loop; `mutation_slot` becomes the event-list
  index; update the slot comment.
- `CellEvoX/include/systems/CommonPopulationStep.hpp` and
  `CellEvoX/src/systems/SimulationEngine.cpp` — keep the same call site in both
  modes, no divergent logic.
- `CellEvoX/include/systems/SimulationEngine3DCapacity.hpp` /
  `.cpp` — `total_mutation_probability` plumbing extended with `Λ`.
- `CellEvoX/src/core/RunDataEngine.cpp` — per-distinct-allele VAF in both the
  in-memory and binary-snapshot statistic paths.
- `CellEvoX/src/ecs/Run.cpp` — counters already iterate records; confirm
  `driver/positive/neutral/negative_mutations` semantics under k > 1 and the
  neutral-band comparison that ADR-0012 introduced.
- `web/backend/results_parser.py` — `_driver_signature` deduplicates by
  `(mutation_id, mutation_type)` and renders the hybrid token; make
  `_muller_parent_signature` robust to `id:type` tokens and repeated ids;
  `web/backend/main.py` schema and
  `web/frontend/src/components/config/MutationEditor.tsx` /
  `web/frontend/src/types/simulation.ts` — per-type `count_model` toggle and
  `lambda` field (the `effect_mode` UI is already there).
- `CellEvoX/scripts/snapshot_io.py` — `driver_signature_from_driver_ids`
  currently collapses ids through a `set`; it must key by `(id, type)` and use
  the same hybrid rendering as the backend.
- `CellEvoX/scripts/plot_muller.py`, `plot_phylogeny.py`,
  `plot_clone_counts.py`, `plot_clone_lifespans.py` — reuse the same hybrid
  rendering; `plot_clone_counts.py` / `plot_clone_lifespans.py` now return the
  rendered label (a string) instead of a driver-id tuple.
- `CellEvoX/tests/fixtures/mini_run/` and
  `CellEvoX/tests/test_population_snapshot_io.cpp` — v3 fixtures stay valid;
  add hybrid/duplicate cases.
- `docs/output-contract.md` — state that `mutation_id` is not unique within a
  cell, that duplicates are independent occurrences, and how VAF/clone keys are
  derived.

## Resolved questions (2026-11-20)
1. **Allele identity in outputs.** Decided: alleles are keyed by
   `(mutation_id, mutation_type)` everywhere, but the rendered clone token stays
   the bare `mutation_id` unless that id carries more than one type in the cell,
   in which case it becomes `mutation_id:mutation_type`. Existing runs are not
   relabelled; hybrid runs stay lossless. See "Downstream semantics and output
   impact".
2. **Cap on k.** Decided: no cap. `lambda` is validated as finite and `>= 0`
   only; the Poisson tail is left to the sampler.
3. **Extra observability.** Decided: no `mutations_per_division` histogram in
   this iteration; `MutationCount` is sufficient and a histogram can be a later,
   independent change.

## Evidence
- `CellEvoX/tests/test_common_population_step.cpp` —
  "CommonPopulationStep applies Bernoulli and Poisson mutations in one division"
  (one Bernoulli + one Poisson type, fixed seed: Bernoulli first, single-id
  stamping, cumulative fitness product, repeatable payload) and
  "CommonPopulationStep keeps the Bernoulli mutation in gamma effect slot zero"
  (per-slot `substream(3, m)` effects). "Deterministic poisson draws are
  reproducible across both sampling regimes" pins inversion (`lambda < 10`) and
  PTRS (`lambda >= 10`) reproducibility and means.
- `CellEvoX/tests/test_simulation.cpp` — "SimulationConfig parses hybrid mutation
  count models" and "SimulationConfig rejects invalid mutation count models"
  (missing `count_model` stays Bernoulli, `probability`/`lambda` exclusivity,
  unknown model, negative `lambda`, Bernoulli pool bound).
- `CellEvoX/tests/test_common_population_step.cpp` parity test (existing, now
  passing with `total_mutation_rate`) keeps 2D/3D event payloads identical; the
  full `CellEvoXTests "~[benchmark]"` suite passes (58 cases, 237956
  assertions).
- `CellEvoX/scripts/test_snapshot_io.py` — `HybridCloneSignatures` covers the
  bare-id render, the `id:type` collision render, deduplication of repeated
  occurrences, and driver filtering.
- `web/backend/tests/test_results_parser.py` — clone-signature rendering, parent
  fallback for `id:type` tokens; `web/backend/tests/test_api.py` — hybrid config
  accepted and `count_model`/`probability`/`lambda` validation mirrors C++
  (44 backend tests pass).
