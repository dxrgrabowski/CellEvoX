#include "systems/MutationEffect.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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

void appendMutatedDaughters(std::vector<Cell>& new_cells, const Cell& cell, double fitness,
                            double mutation_draw,
                            const std::map<uint8_t, MutationType>& available_mutation_types,
                            uint64_t seed, uint64_t rng_step, uint64_t cell_id) {
  double prob_sum = 0.0;
  for (const auto& mut : available_mutation_types) {
    prob_sum += mut.second.probability;
    if (mutation_draw < prob_sum) {
      const float realized_effect =
          realizedMutationEffect(mut.second, seed, rng_step, cell_id, 0);
      const double daughter_fitness = fitness * (1.0 + realized_effect);
      if (!std::isfinite(daughter_fitness) || daughter_fitness <= 0.0) {
        throw std::runtime_error("Mutation produced invalid daughter fitness");
      }
      Cell daughter_cell1 = Cell(cell, daughter_fitness);
      daughter_cell1.mutations.push_back({0, mut.second.type_id, realized_effect});
      new_cells.push_back(std::move(daughter_cell1));
      new_cells.emplace_back(cell, fitness);
      return;
    }
  }
}

}  // namespace CellEvoX::systems::detail
