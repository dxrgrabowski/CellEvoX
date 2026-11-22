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
  mutation payload is already variable-length, so k > 1 needs no new length
  fields. Version 3 carries `(mutation_id, mutation_type, effect)` but cannot
  distinguish two same-type mutations from one division; the payload therefore
  gains an event-order `slot` and becomes version 4. Version 3 files are not
  kept readable (the simulator has no public deployment yet).
- ADR-0012 (merged as #69) owns RNG stream `3` for per-mutation gamma effects,
  adds `kStreamMutationEffect`, and defines
  `substream(base, slot) = base + slot * 2^32`. Before this ADR,
  `MutationEffect.cpp` passed `mutation_slot = 0` and explicitly reserved slot
  `1+` "for a future second mutation". This ADR fills that reservation and must
  not reuse stream `3`.

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
5. Each mutation's placeholder `{0, type_id, slot, effect}` is appended in list
   order and rewritten to A's id after id assignment, where `slot` is the
   mutation's index in the event list (Bernoulli `0`, Poisson `1..k`, or
   `0..k-1` without a Bernoulli hit).
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

`slot` is a `uint16`, so a division can realize at most 65 536 mutations and the
highest effect substream key is `3 + 65535 * 2^32` (~2^48). A
`static_assert(substream(kStreamMutationEffect, 0xFFFF) < kStreamMutationCount)`
pins that bound, so no gamma effect can ever alias the count stream's PTRS
uniforms. `appendMutatedDaughters` enforces the bound at run time: a Poisson
count that does not fit the slot range aborts the run with
`std::overflow_error` instead of wrapping and aliasing two alleles.

### Allele identity and output impact
Mutations from one division share `mutation_id` (the origin daughter) and differ
in `mutation_type`, `slot` and `effect`. `Cell::mutations` is now
`std::vector<CellMutation{mutation_id, mutation_type, slot, effect}>`.

`mutation_type` alone cannot identify an occurrence: two Poisson draws in the
same division can select the same type, and they are independent alleles
(different gamma draws, different RNG slots). `slot` is the position in the
division's event list, so `(mutation_id, slot)` is unique within a cell and
stable under driver-only filtering; `mutation_type` stays an attribute of the
allele.

`slot` is stored rather than reconstructed: the same value is already computed
for the gamma substream in `appendMutatedDaughters`, so persisting it adds no
RNG bookkeeping and no per-record bytes (see the `CellMutation` layout note
below). Deriving it from the position in a filtered payload would be fragile.

Consumers written for "one mutation per cell per id" break as follows:

- **VAF inflates.** `RunDataEngine::plotMutationFrequency` increments
  `mutation_counts[mutation_id]` per record, in both the in-memory path and the
  binary-snapshot path. A cell carrying two occurrences of one origin id then
  counts twice, so a VAF can exceed `1`. It must count the *presence* of an
  allele per cell, keyed by `(mutation_id, slot)`.
- **Clone signatures diverge.** `results_parser._driver_signature` sorts origin
  ids without deduplicating, so a cell with driver alleles `(5,0)` and `(5,1)`
  yields `"5,5"`, while `snapshot_io.driver_signature_from_driver_ids` uses a
  set and yields `"5"`. The same run therefore gets different clone labels
  depending on the reader.
- **Muller parent search is fragile.** `_muller_parent_signature` drops the
  last comma token and assumes ids are monotonic; repeated ids make that
  heuristic ambiguous.

The structural payload stays valid apart from the new identifier:
`MutationCount` remains the number of records, `mutations_count` /
`driver_mutation_count` remain counts, and the mutation record grows by one
`uint16 slot` (9 -> 11 bytes packed, snapshot version 4). The CSV cell payload
grows from `(id,type,effect)` to `(id,type,effect,slot)`.

Recommended output rule:

- an allele is `(mutation_id, slot)`; a cell either carries it or not;
- VAF = cells carrying the allele / total cells (deduplicate per cell);
- `MutationCount` = occurrences, including two hits of the same allele;
- clone signatures and Muller ancestry use the deduplicated allele set;
- `docs/output-contract.md` states that `mutation_id` is not unique within a
  cell, that `slot` is the position in the division's mutation list, and that
  duplicates are independent occurrences, not a data error.

Clone-signature labels are hybrid, so existing runs keep their labels while
hybrid runs lose nothing:

- deduplicate by the allele key `(mutation_id, slot)` in every reader;
- render a token as the bare `mutation_id` when that id carries exactly one
  allele in the cell (the only case that exists in pre-#59 and in
  Bernoulli-only data, where every mutation has `slot = 0`);
- render it as `mutation_id:slot` only when the same id appears more than once
  in that cell;
- both `results_parser._driver_signature` and
  `snapshot_io.driver_signature_from_driver_ids` must implement exactly this
  rule, and `_muller_parent_signature` must drop a whole id group, not assume
  every token is a bare integer.

`CellMutation` layout: the pre-#59 fields are `uint32_t mutation_id`,
`uint8_t mutation_type`, `float effect` (12 bytes including 3 bytes of padding).
Adding `uint16_t slot` between `mutation_type` and `effect` fills that padding,
so `sizeof(CellMutation)` stays 12 and per-cell mutation memory does not grow
for the same mutation count.

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
  `(mutation_id, slot)`, not per record; both the C++ statistics path and
  `web/backend/results_parser.py` are in scope. Clone tokens keep the bare id
  unless one id appears several times in the cell, so pre-#59 outputs are
  unaffected.
- The mutation payload is version 4: the packed record is
  `(mutation_id, mutation_type, slot, effect)` (11 bytes) and CSV rows use
  `(id,type,effect,slot)`. Version 3 snapshots are rejected by the reader.
- `total_mutations_memory` grows with the average number of mutations per
  division; `sizeof(CellMutation)` itself stays 12 bytes, so no new memory
  accounting is introduced.
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
  `effect_limit` already exist from ADR-0012. Add `uint16_t slot` to
  `CellMutation` between `mutation_type` and `effect` so the struct stays 12
  bytes.
- `CellEvoX/include/io/PopulationSnapshotIO.hpp` — bump
  `kPopulationSnapshotVersion` to 4 and add `uint16_t slot` to
  `PopulationSnapshotDriverMutation` (packed size 11).
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
  `(mutation_id, slot)` and renders the hybrid token; make
  `_muller_parent_signature` robust to `id:slot` tokens and repeated ids;
  `web/backend/main.py` schema and
  `web/frontend/src/components/config/MutationEditor.tsx` /
  `web/frontend/src/types/simulation.ts` — per-type `count_model` toggle and
  `lambda` field (the `effect_mode` UI is already there).
- `CellEvoX/scripts/snapshot_io.py` — `driver_signature_from_driver_ids`
  currently collapses ids through a `set`; it must key by `(id, slot)` and use
  the same hybrid rendering as the backend. `parse_mutations` returns
  `(id, type, effect, slot)`.
- `CellEvoX/scripts/plot_muller.py`, `plot_phylogeny.py`,
  `plot_clone_counts.py`, `plot_clone_lifespans.py` — reuse the same hybrid
  rendering and tolerate the extra CSV field; `plot_clone_counts.py` /
  `plot_clone_lifespans.py` now return the rendered label (a string) instead of
  a driver-id tuple.
- `CellEvoX/tests/fixtures/mini_run/` and
  `CellEvoX/tests/test_population_snapshot_io.cpp` — fixtures move to the v4
  payload; add hybrid/same-type duplicate cases.
- `docs/output-contract.md` — state that `mutation_id` is not unique within a
  cell, what `slot` means, that duplicates are independent occurrences, and how
  VAF/clone keys are derived.

## Resolved questions (2026-11-20)
1. **Allele identity in outputs.** Decided: alleles are keyed by
   `(mutation_id, slot)` everywhere, where `slot` is the mutation's index in the
   division's event list (`0` for a Bernoulli hit, `0..k-1` for Poisson-only
   events, `1..k` after a Bernoulli hit). The rendered clone token stays the
   bare `mutation_id` unless that id appears more than once in the cell, in
   which case it becomes `mutation_id:slot`. Existing runs are not relabelled;
   hybrid runs stay lossless. This supersedes an earlier plan to key alleles by
   `(mutation_id, mutation_type)`, which merged two same-type Poisson draws from
   one division.
2. **Cap on k.** Decided: no cap on `lambda` itself. `lambda` is validated as
   finite and `>= 0` only; the Poisson tail is left to the sampler. The one hard
   limit is structural: a realised count must fit the `uint16` slot space, so a
   division that would exceed 65 536 mutations is rejected at run time rather
   than silently wrapped.
3. **Extra observability.** Decided: no `mutations_per_division` histogram in
   this iteration; `MutationCount` is sufficient and a histogram can be a later,
   independent change.
4. **Snapshot compatibility.** Decided: v3 snapshots need not stay readable. The
   payload gains `slot` and moves to version 4; the reader rejects version 3
   with the existing header check.
5. **Memory.** Decided: `slot` is a `uint16` placed in the struct's existing
   padding, so `sizeof(CellMutation)` stays 12 bytes and per-cell mutation
   memory is unchanged for the same mutation count.

## Evidence
- `CellEvoX/tests/test_common_population_step.cpp` —
  "CommonPopulationStep applies Bernoulli and Poisson mutations in one division"
  (one Bernoulli + one Poisson type, fixed seed: Bernoulli first, single-id
  stamping, cumulative fitness product, repeatable payload) and
  "CommonPopulationStep keeps the Bernoulli mutation in gamma effect slot zero"
  (per-slot `substream(3, m)` effects). "Deterministic poisson draws are
  reproducible across both sampling regimes" pins inversion (`lambda < 10`) and
  PTRS (`lambda >= 10`) reproducibility and means, and "Poisson count and type
  draws use disjoint stream keys" pins the `kStreamMutationCount`/`Type` split
  and that every reachable effect substream stays below the count stream.
- `CellEvoX/tests/test_common_population_step.cpp` — "Mutation rejects a division
  that exceeds the 16-bit allele slot limit" pins the run-time rejection of a
  Poisson count that cannot fit `uint16` slots.
- `CellEvoX/tests/test_simulation.cpp` — "SimulationConfig parses hybrid mutation
  count models" and "SimulationConfig rejects invalid mutation count models"
  (missing `count_model` stays Bernoulli, `probability`/`lambda` exclusivity,
  unknown model, negative `lambda`, Bernoulli pool bound).
- `CellEvoX/tests/test_common_population_step.cpp` parity test (existing, now
  passing with `total_mutation_rate`) keeps 2D/3D event payloads identical; the
  full `CellEvoXTests "~[benchmark]"` suite passes (58 cases, 237956
  assertions).
- `CellEvoX/scripts/test_snapshot_io.py` — `HybridCloneSignatures` covers the
  bare-id render, the `id:slot` collision render (including two same-type draws
  in one division), deduplication of repeated occurrences, and driver filtering.
- `web/backend/tests/test_results_parser.py` — clone-signature rendering, parent
  fallback for `id:slot` tokens; `web/backend/tests/test_api.py` — hybrid config
  accepted and `count_model`/`probability`/`lambda` validation mirrors C++
  (44 backend tests pass).
