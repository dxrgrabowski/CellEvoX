#pragma once

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "systems/SimulationEngine.hpp"
#include "utils/ParallelAlgorithms.hpp"
#include "utils/PhaseProfiler.hpp"

namespace CellEvoX::systems {

struct CommonBirthEvent {
  uint32_t id;
  uint32_t parent_id;
};

struct CommonDeathEvent {
  uint32_t id;
  uint32_t parent_id;
};

struct CommonPopulationStepResult {
  std::vector<CommonBirthEvent> births;
  std::vector<CommonDeathEvent> deaths;
};

inline Eigen::VectorXd generateExponentialDistribution(size_t size, std::mt19937& rng) {
  std::exponential_distribution<> exp_dist(1.0);

  Eigen::VectorXd result(static_cast<Eigen::Index>(size));
  for (size_t i = 0; i < size; ++i) {
    result(static_cast<Eigen::Index>(i)) = exp_dist(rng);
  }

  return result;
}

inline bool commonDaughterCellLess(const Cell& lhs, const Cell& rhs) {
  if (lhs.parent_id != rhs.parent_id) return lhs.parent_id < rhs.parent_id;
  if (lhs.fitness != rhs.fitness) return lhs.fitness < rhs.fitness;
  if (lhs.mutations.size() != rhs.mutations.size()) {
    return lhs.mutations.size() < rhs.mutations.size();
  }
  if (!lhs.mutations.empty() && !rhs.mutations.empty()) {
    return lhs.mutations.back().second < rhs.mutations.back().second;
  }
  return false;
}

inline CommonPopulationStepResult applyCommonPopulationStep(
    CellMap& cells,
    Graveyard& cells_graveyard,
    const SimulationConfig& config,
    const std::map<uint8_t, MutationType>& available_mutation_types,
    double total_mutation_probability,
    size_t& actual_population,
    size_t& total_deaths,
    double tau,
    std::mt19937& rng) {
  CELLEVOX_PROFILE_PHASE("common_step_total");
  CommonPopulationStepResult result;

  const double tau_step = config.tau_step;
  const size_t Nc = config.env_capacity;
  if (Nc == 0) {
    actual_population = cells.size();
    return result;
  }
  if (!std::isfinite(total_mutation_probability) || total_mutation_probability < 0.0 ||
      total_mutation_probability > 1.0) {
    throw std::invalid_argument("total_mutation_probability must be finite and in [0, 1]");
  }

  std::vector<uint32_t> alive_cell_indices;
  {
    CELLEVOX_PROFILE_PHASE("collect_alive_ids");
    alive_cell_indices.reserve(cells.size());
    for (auto it = cells.begin(); it != cells.end(); ++it) {
      alive_cell_indices.push_back(it->first);
    }
  }

  {
    CELLEVOX_PROFILE_PHASE("sort_alive_ids");
    CellEvoX::parallel_algorithms::sortMaybeParallel(alive_cell_indices.begin(), alive_cell_indices.end());
  }

  if (alive_cell_indices.empty()) {
    actual_population = 0;
    return result;
  }

  if (alive_cell_indices.size() != actual_population) {
    spdlog::error("Mismatch in alive cell count: expected {}, found {}",
                  actual_population,
                  alive_cell_indices.size());
    actual_population = alive_cell_indices.size();
  }

  const size_t N = alive_cell_indices.size();

  const double scaling_factor = static_cast<double>(N) / static_cast<double>(Nc);
  Eigen::VectorXd death_probs;
  {
    CELLEVOX_PROFILE_PHASE("rng_death");
    death_probs = generateExponentialDistribution(N, rng) / scaling_factor;
  }

  Eigen::VectorXd birth_randoms;
  {
    CELLEVOX_PROFILE_PHASE("rng_birth");
    birth_randoms = generateExponentialDistribution(N, rng);
  }

  if (death_probs.size() != static_cast<Eigen::Index>(N) ||
      birth_randoms.size() != static_cast<Eigen::Index>(N)) {
    spdlog::error("Death arr: {} B: {} AP: {}", death_probs.size(), birth_randoms.size(), N);
  }

  std::vector<double> mutation_rand_vals(N);
  {
    CELLEVOX_PROFILE_PHASE("rng_mutation");
    std::uniform_real_distribution<> unif_dist(0.0, 1.0);
    for (size_t i = 0; i < N; ++i) {
      mutation_rand_vals[i] = unif_dist(rng);
    }
  }

  tbb::concurrent_vector<Cell> new_cells;
  tbb::concurrent_vector<CommonDeathEvent> dead_cells;

  {
    CELLEVOX_PROFILE_PHASE("parallel_events");
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, alive_cell_indices.size()),
        [&](const tbb::blocked_range<size_t>& range) {
          for (size_t i = range.begin(); i != range.end(); ++i) {
            const uint32_t idx = alive_cell_indices[i];
            CellMap::const_accessor cell;
            if (!cells.find(cell, idx)) {
              continue;
            }

            if (death_probs[static_cast<Eigen::Index>(i)] <= tau_step) {
              dead_cells.push_back({idx, cell->second.parent_id});
              continue;
            }

            const double fitness = cell->second.fitness;
            if (!std::isfinite(fitness) || fitness <= 0.0) {
              spdlog::error("Cell {} has invalid fitness {}; skipping birth event", idx, fitness);
              continue;
            }

            const double birth_prob = birth_randoms[static_cast<Eigen::Index>(i)] / fitness;
            if (birth_prob > tau_step) {
              continue;
            }

            dead_cells.push_back({idx, cell->second.parent_id});

            const double rand_val = mutation_rand_vals[i];
            if (rand_val >= total_mutation_probability) {
              new_cells.emplace_back(cell->second, cell->second.fitness);
              new_cells.emplace_back(cell->second, cell->second.fitness);
              continue;
            }

            double prob_sum = 0.0;
            for (const auto& mut : available_mutation_types) {
              prob_sum += mut.second.probability;
              if (rand_val < prob_sum) {
                const double daughter_fitness = fitness * (1.0 + mut.second.effect);
                if (!std::isfinite(daughter_fitness) || daughter_fitness <= 0.0) {
                  throw std::runtime_error("Mutation produced invalid daughter fitness");
                }
                Cell daughter_cell1 =
                    Cell(cell->second, daughter_fitness);
                daughter_cell1.mutations.push_back({0, mut.second.type_id});
                new_cells.push_back(std::move(daughter_cell1));
                new_cells.emplace_back(cell->second, fitness);
                break;
              }
            }
          }
        });
  }

  std::vector<Cell> sorted_new_cells;
  {
    CELLEVOX_PROFILE_PHASE("copy_sort_births");
    sorted_new_cells.reserve(new_cells.size());
    for (auto& cell : new_cells) {
      sorted_new_cells.push_back(std::move(cell));
    }

    CellEvoX::parallel_algorithms::sortMaybeParallel(
        sorted_new_cells.begin(), sorted_new_cells.end(), commonDaughterCellLess);
  }

  const auto max_cell_id = std::numeric_limits<uint32_t>::max();
  if (total_deaths > max_cell_id || N > max_cell_id - total_deaths) {
    throw std::overflow_error("Cell id space exhausted before assigning birth ids");
  }

  const uint32_t starting_id = static_cast<uint32_t>(N + total_deaths);
  const size_t remaining_ids =
      static_cast<size_t>(max_cell_id) - static_cast<size_t>(starting_id) + 1;
  if (sorted_new_cells.size() > remaining_ids) {
    throw std::overflow_error("Cell id space exhausted while assigning birth ids");
  }

  {
    CELLEVOX_PROFILE_PHASE("apply_births");
    result.births.reserve(sorted_new_cells.size());
    for (size_t i = 0; i < sorted_new_cells.size(); ++i) {
      const uint32_t new_id = starting_id + static_cast<uint32_t>(i);
      sorted_new_cells[i].id = new_id;

      CellMap::accessor accessor;
      if (!cells.insert(accessor, {new_id, std::move(sorted_new_cells[i])})) {
        spdlog::error("Failed to insert new cell {}", new_id);
        continue;
      }

      for (auto& mutation : accessor->second.mutations) {
        if (mutation.first == 0) {
          mutation.first = new_id;
        }
      }

      result.births.push_back({new_id, accessor->second.parent_id});
    }
  }

  {
    CELLEVOX_PROFILE_PHASE("sort_deaths");
    result.deaths.reserve(dead_cells.size());
    for (const auto& death : dead_cells) {
      result.deaths.push_back(death);
    }
    CellEvoX::parallel_algorithms::sortMaybeParallel(
        result.deaths.begin(), result.deaths.end(), [](const auto& lhs, const auto& rhs) {
          return lhs.id < rhs.id;
        });
  }

  {
    CELLEVOX_PROFILE_PHASE("apply_deaths");
    for (const auto& death : result.deaths) {
      cells_graveyard.insert({death.id, {death.parent_id, tau}});
      cells.erase(death.id);
    }
  }

  const size_t removed_count = dead_cells.size();
  if (removed_count > static_cast<size_t>(max_cell_id) - total_deaths) {
    throw std::overflow_error("Cell death counter exceeds uint32_t cell id space");
  }
  total_deaths += removed_count;
  actual_population = cells.size();

  return result;
}

}  // namespace CellEvoX::systems
