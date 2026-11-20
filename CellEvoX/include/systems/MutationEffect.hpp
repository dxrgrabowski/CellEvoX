#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "ecs/Cell.hpp"

namespace CellEvoX::systems::detail {

// Gamma draw for one mutation event. Defined out of line on purpose: the
// sampler body (shape/scale computation, substream selection, clamp) must not
// be inlined into the per-cell event loop of `applyCommonPopulationStep`, where
// it would slow down the constant/no-mutation fast path through code bloat. The
// mode dispatch itself stays in the inline `realizedMutationEffect` wrapper
// below, so a constant mutation is one branch plus a field read.
float sampleGammaMutationEffect(const MutationType& mutation, uint64_t seed, uint64_t step,
                                uint64_t cell_id, uint32_t mutation_slot);

// Realized signed fitness effect of one mutation event. Constant mutations use
// the configured value and consume no RNG, so constant-only runs stay
// bit-identical to previous releases. Gamma mutations draw from a dedicated
// stream/substream (never streams 0/1/2) and clamp the magnitude to
// `effect_limit`. `mutation_slot` reserves room for multiple mutations per
// division without shifting existing draws.
inline float realizedMutationEffect(const MutationType& mutation, uint64_t seed, uint64_t step,
                                    uint64_t cell_id, uint32_t mutation_slot) {
  if (mutation.effect_mode != MutationEffectMode::Gamma) {
    return mutation.effect;
  }
  return sampleGammaMutationEffect(mutation, seed, step, cell_id, mutation_slot);
}

// Appends the two daughters of one birth event to `new_cells`: the mutated
// daughter (placeholder mutation id 0, rewritten by the caller) followed by the
// unmutated daughter. The function owns both mutation pools from ADR-0013:
// the legacy Bernoulli pool (stream 2, at most one mutation) followed by the
// Poisson pool (stream 4 count, stream 5 per-mutation type). All mutations go
// to daughter A; daughter B is always an unmutated copy of the parent. The
// `mutation_slot` of each realized effect is its index in the event list, so a
// Bernoulli mutation always takes slot 0 and Poisson mutations follow.
// Defined out of line on purpose: the pools, effect sampling and daughter
// construction must not sit in the per-cell event loop of
// `applyCommonPopulationStep`, where they change register allocation and code
// size of the no-mutation fast path.
void appendMutatedDaughters(std::vector<Cell>& new_cells, const Cell& cell, double fitness,
                            const std::map<uint8_t, MutationType>& available_mutation_types,
                            double total_mutation_probability, double total_mutation_rate,
                            uint64_t seed, uint64_t rng_step, uint64_t cell_id);

}  // namespace CellEvoX::systems::detail
