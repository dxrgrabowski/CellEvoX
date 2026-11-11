#pragma once

#include <cstdint>

#include "ecs/Cell.hpp"

namespace CellEvoX::systems::detail {

// Gamma draw for one mutation event. Defined out of line on purpose: the
// sampler setup (shape/scale computation, substream selection, clamp) must not
// be inlined into the per-cell event loop of `applyCommonPopulationStep`, where
// it would slow down the constant/no-mutation fast path through code bloat.
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

}  // namespace CellEvoX::systems::detail
