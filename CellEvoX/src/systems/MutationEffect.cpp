#include "systems/MutationEffect.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

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
                            const std::map<uint8_t, MutationType>& available_mutation_types,
                            double total_mutation_probability, double total_mutation_rate,
                            uint64_t seed, uint64_t rng_step, uint64_t cell_id) {
  // Realized mutations in event-list order. The Bernoulli mutation (if any)
  // comes first, so it always occupies slot 0; Poisson mutations follow.
  std::vector<CellMutation> new_mutations;

  // 1. Bernoulli pool. Skipped entirely when empty, so legacy configs read the
  //    same stream 2 draw exactly once and stay bit-identical.
  if (total_mutation_probability > 0.0) {
    const double mutation_draw = CellEvoX::deterministic_rng::uniform01(
        seed, rng_step, cell_id, CellEvoX::deterministic_rng::kStreamMutation);
    if (mutation_draw < total_mutation_probability) {
      double prob_sum = 0.0;
      const MutationType* selected = nullptr;
      for (const auto& [type_id, mutation] : available_mutation_types) {
        if (mutation.count_model != MutationCountModel::Bernoulli) {
          continue;
        }
        prob_sum += mutation.probability;
        if (mutation_draw < prob_sum) {
          selected = &mutation;
          break;
        }
      }
      if (selected != nullptr) {
        const auto slot = static_cast<uint16_t>(new_mutations.size());
        const float realized_effect =
            realizedMutationEffect(*selected, seed, rng_step, cell_id, slot);
        new_mutations.push_back({0, selected->type_id, slot, realized_effect});
      }
    }
  }

  // 2. Poisson pool. Skipped entirely when empty, so Bernoulli-only runs never
  //    read the count/type streams.
  if (total_mutation_rate > 0.0) {
    const uint64_t count = CellEvoX::deterministic_rng::poisson01(
        seed, rng_step, cell_id,
        CellEvoX::deterministic_rng::substream(
            CellEvoX::deterministic_rng::kStreamMutationCount, 0),
        total_mutation_rate);
    // `slot` is a uint16 allele key. A count that does not fit would wrap and
    // collide two alleles of the same id, so reject it instead (ADR-0013).
    constexpr uint64_t kMaxMutationsPerDivision =
        static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()) + 1ULL;
    if (count > kMaxMutationsPerDivision - new_mutations.size()) {
      throw std::overflow_error(
          "mutation count per division exceeds the 16-bit allele slot limit (65536)");
    }
    for (uint64_t j = 0; j < count; ++j) {
      const double type_draw = CellEvoX::deterministic_rng::uniform01(
          seed, rng_step, cell_id,
          CellEvoX::deterministic_rng::substream(
              CellEvoX::deterministic_rng::kStreamMutationType,
              static_cast<uint32_t>(j)));
      const double target = type_draw * total_mutation_rate;
      double cumulative_rate = 0.0;
      const MutationType* selected = nullptr;
      for (const auto& [type_id, mutation] : available_mutation_types) {
        if (mutation.count_model != MutationCountModel::Poisson) {
          continue;
        }
        cumulative_rate += static_cast<double>(mutation.lambda);
        if (target < cumulative_rate) {
          selected = &mutation;
          break;
        }
      }
      if (selected == nullptr) {
        // Floating-point rounding can leave target at or above the cumulative
        // sum. Fall back to the last Poisson type with positive weight.
        for (auto it = available_mutation_types.rbegin();
             it != available_mutation_types.rend(); ++it) {
          if (it->second.count_model == MutationCountModel::Poisson &&
              it->second.lambda > 0.0f) {
            selected = &it->second;
            break;
          }
        }
      }
      if (selected == nullptr) {
        throw std::logic_error("Poisson mutation count requires a type with positive lambda");
      }
      const auto slot = static_cast<uint16_t>(new_mutations.size());
      const float realized_effect =
          realizedMutationEffect(*selected, seed, rng_step, cell_id, slot);
      new_mutations.push_back({0, selected->type_id, slot, realized_effect});
    }
  }

  if (new_mutations.empty()) {
    new_cells.emplace_back(cell, cell.fitness);
    new_cells.emplace_back(cell, cell.fitness);
    return;
  }

  // Sequential product in event-list order. Every step must stay finite and > 0.
  double daughter_fitness = fitness;
  for (const auto& mutation : new_mutations) {
    daughter_fitness *= 1.0 + static_cast<double>(mutation.effect);
    if (!std::isfinite(daughter_fitness) || daughter_fitness <= 0.0) {
      throw std::runtime_error("Mutation produced invalid daughter fitness");
    }
  }
  // Cell stores fitness as float; a positive double below float range would
  // round to 0.0f, so validate the value that is actually stored.
  const float stored_fitness = static_cast<float>(daughter_fitness);
  if (!std::isfinite(stored_fitness) || stored_fitness <= 0.0f) {
    throw std::runtime_error("Mutation produced invalid daughter fitness");
  }

  Cell daughter_cell1 = Cell(cell, daughter_fitness);
  daughter_cell1.mutations.insert(daughter_cell1.mutations.end(),
                                  new_mutations.begin(),
                                  new_mutations.end());
  new_cells.push_back(std::move(daughter_cell1));
  new_cells.emplace_back(cell, fitness);
}

}  // namespace CellEvoX::systems::detail
