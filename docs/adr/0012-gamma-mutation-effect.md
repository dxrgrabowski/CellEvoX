# ADR-0012: Configurable gamma-distributed mutation effects
Status: Accepted · 2026-11-11

## Context
A constant per-mutation `effect` makes every identical mutation exactly equally
fit (or deleterious), which cannot express the empirical finding that mutation
effects are approximately gamma-distributed with a heavy right tail. The
simulation needs a per-mutation switch between the existing constant effect and a
sampled effect, without weakening the determinism contract or changing the
meaning of existing configs.

## Decision
`MutationType` gains an `effect_mode`:

- `constant` (default when the key is absent) keeps the existing `effect`, and
  consumes no new randomness.
- `gamma` samples one effect per mutation event from
  `X ~ Gamma(k, θ)` with `k = (mean/std)²`, `θ = std²/mean`, taken from a new
  RNG stream (id `3`, see below), then applies
  `signed_effect = direction_sign * min(X, effect_limit)` and
  `daughter_fitness = fitness * (1 + signed_effect)`.

`effect_limit` is a hard cap, so the realized mean is biased downward relative to
the requested `effect_mean`; this is documented in the runbook. `effect_limit`
must be in `(0, 1)` and at least `effect_mean`, so the clamp never defines the
whole distribution and a daughter's fitness stays positive; `effect_mean` and
`effect_std` must be positive. Both directions are capped the same way.

The realized per-mutation effect is persisted in `Cell::mutations` and in every
exported mutation payload — driver mutations only, unless
`full_mutation_payload` is enabled — so classification (positive/negative/neutral)
reflects the sampled value rather than the configured direction.

### RNG stream
Stream ids `0/1/2` stay untouched. Stream `3` is the mutation-effect stream, and
its substreams are keyed by `(purpose, mutation slot)`:
`substream(3, slot) = 3 + slot * 2^32`, leaving the low 32 bits as the draw
index. A gamma draw consumes one or two uniforms, or Box–Muller normals for the
`k >= 1` Marsaglia–Tsang path; all are pure functions of
`(seed, step, cell_id, substream)`. Constant-only runs therefore consume the same
draws as before and stay bit-identical.

### Neutral band
In short: an effect too small for a `float` fitness to record as a distinct
value is reported as neutral, so the positive/negative counters are not decided
by the last rounding bit.

Classification compares the realized effect against a configurable band
`mutation_effect_neutral_tolerance`: `|effect| <= tolerance` is reported as
neutral, otherwise the sign decides. The default is
`std::numeric_limits<float>::epsilon()` (`≈1.19e-7`), and the value must be in
`[0, 1)`.

The default is a reporting threshold, not a guarantee that fitness is unchanged.
`Cell::fitness` and `CellMutation::effect` are `float`, and next to `1.0` the gap
to the following representable value — one unit in the last place (ULP) — is
`2^-23 ≈ 1.19e-7`, i.e. exactly one float epsilon. An effect at or below this gap
is at the resolution limit of the stored fitness, so its sign is dominated by
rounding rather than by the model. A representable change can still fall inside
the band — for instance `fitness = 1.0f` plus one epsilon — and be counted
neutral; the rationale is that such a change is within a single rounding step of
the state and its sign is not trustworthy, not that the state provably stayed
put.

A larger band is a modelling statement, not a numerical one. `1e-3`, for example,
is ~8400× float epsilon and would call a fully representable 0.1% fitness change
neutral. That is defensible as a drift threshold — selection is conventionally
treated as negligible when `|s| < 1/(2N_e)`, which for `N_e = 1000` sits at
`5e-4` — but the right value follows from the effective population size and the
fitness scale of a particular study, not from a universal constant. It is
therefore an explicit override (`mutation_effect_neutral_tolerance` in the
config JSON) and not the default.

## Consequences
- Adding or reordering mutation effects changes the gamma draws from that point
  on; constant-only configurations remain bit-identical to the previous
  contract.
- The gamma sampler must stay internal to `DeterministicRng.hpp`
  (Marsaglia–Tsang for `k >= 1`, `U^(1/k)` boosting below that); using
  `std::gamma_distribution` or a shared engine would break thread-count
  independence.
- The snapshot ABI moves to `CELXPOP1` v3 and the `Mutations` CSV column to
  `(mutation_id,mutation_type,effect)`. Every in-repo reader is updated in the
  same commit; old runs are not kept readable.
- The cap is a deliberate modelling bias, not a bug: a downstream comparison of
  configured `effect_mean` against achieved mean fitness must account for it.
- The neutral band only affects reported counters; it does not change the
  fitness update or the stored effect. Raising it therefore leaves dynamics
  bit-identical and only reclassifies the positive/negative/neutral tallies.
- Multiple mutations per division event remain out of scope.

## Evidence
- Sampler: `CellEvoX/include/utils/DeterministicRng.hpp` (`gamma01`, `normal01`,
  `substream`).
- Application: `CellEvoX/include/systems/MutationEffect.hpp` (`realizedMutationEffect`)
  and `CellEvoX/src/systems/MutationEffect.cpp` (out-of-line gamma draw, so the
  event loop's no-mutation fast path stays lean), shared by both live modes.
- Config validation: `CellEvoX/include/utils/SimulationConfig.hpp`
  (`mutation_effect_neutral_tolerance` must be finite and in `[0, 1)`).
- Neutral-band classification: `CellEvoX/src/ecs/Run.cpp` (`processRunInfo`); the
  value is threaded from `SimulationConfig` through `SimulationEngine::run`.
- Tests: `CellEvoX/tests/test_simulation.cpp` ("Run neutral band is configurable
  and overridable") and `web/backend/tests/test_api.py`.
- ABI sizes pinned by `static_assert`s in
  `CellEvoX/include/io/PopulationSnapshotIO.hpp`; cross-language fixtures in
  `CellEvoX/tests/fixtures/mini_run/`.
- Owner request: issue #58, 2026-11-11 (including "clamp `min(X, L)`" and
  "reject `effect_limit < effect_mean`" clarifications).
