#include "systems/MutationEffect.hpp"

#include <algorithm>

#include "utils/DeterministicRng.hpp"

namespace CellEvoX::systems::detail {

float sampleGammaMutationEffect(const MutationType& mutation, uint64_t seed, uint64_t step,
                                uint64_t cell_id, uint32_t mutation_slot) {
  const uint64_t stream = CellEvoX::deterministic_rng::substream(
      CellEvoX::deterministic_rng::kStreamMutationEffect, mutation_slot);
  const double draw = CellEvoX::deterministic_rng::gamma01(
      seed, step, cell_id, stream, mutation.gammaShape(), mutation.gammaScale());
  const double magnitude = std::min(draw, static_cast<double>(mutation.effect_limit));
  return static_cast<float>(mutation.directionSign() * magnitude);
}

}  // namespace CellEvoX::systems::detail
