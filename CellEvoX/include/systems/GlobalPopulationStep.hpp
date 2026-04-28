#pragma once

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>

#include <Eigen/Dense>
#include <algorithm>
#include <atomic>
#include <map>
#include <random>
#include <utility>
#include <vector>

#include "systems/SimulationEngine.hpp"
#include "utils/MathUtils.hpp"

namespace CellEvoX::systems {

struct GlobalBirthEvent {
  uint32_t id;
  uint32_t parent_id;
};

struct GlobalDeathEvent {
  uint32_t id;
  uint32_t parent_id;
};

struct GlobalPopulationStepResult {
  std::vector<GlobalBirthEvent> births;
  std::vector<GlobalDeathEvent> deaths;
};

inline Eigen::VectorXd generateExponentialDistribution(size_t size, std::mt19937& rng) {
  std::exponential_distribution<> exp_dist(1.0);

  Eigen::VectorXd result(static_cast<Eigen::Index>(size));
  for (size_t i = 0; i < size; ++i) {
    result(static_cast<Eigen::Index>(i)) = exp_dist(rng);
  }

  return result;
}

inline bool globalDaughterCellLess(const Cell& lhs, const Cell& rhs) {
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

inline GlobalPopulationStepResult applyGlobalPopulationStep(
    CellMap& cells,
    Graveyard& cells_graveyard,
    const SimulationConfig& config,
    const std::map<uint8_t, MutationType>& available_mutation_types,
    double total_mutation_probability,
    size_t& actual_population,
    size_t& total_deaths,
    double tau,
    std::mt19937& rng) {
  GlobalPopulationStepResult result;

  const double tau_step = config.tau_step;
  const size_t N = actual_population;
  const size_t Nc = config.env_capacity;
  if (N == 0 || Nc == 0) {
    actual_population = cells.size();
    return result;
  }

  const double scaling_factor = static_cast<double>(N) / static_cast<double>(Nc);
  Eigen::VectorXd death_probs = generateExponentialDistribution(N, rng) / scaling_factor;

  std::vector<uint32_t> alive_cell_indices;
  alive_cell_indices.reserve(cells.size());
  for (auto it = cells.begin(); it != cells.end(); ++it) {
    alive_cell_indices.push_back(it->first);
  }
  std::sort(alive_cell_indices.begin(), alive_cell_indices.end());

  Eigen::VectorXd birth_probs =
      generateExponentialDistribution(N, rng).array() /
      utils::FitnessCalculator::getCellsFitnessVector(cells, alive_cell_indices).array();

  if (alive_cell_indices.size() != N) {
    spdlog::error(
        "Mismatch in alive cell count: expected {}, found {}", N, alive_cell_indices.size());
  }

  if (death_probs.size() != static_cast<Eigen::Index>(N) ||
      birth_probs.size() != static_cast<Eigen::Index>(N)) {
    spdlog::error("Death arr: {} B: {} AP: {}", death_probs.size(), birth_probs.size(), N);
  }

  std::vector<double> mutation_rand_vals(N);
  std::uniform_real_distribution<> unif_dist(0.0, 1.0);
  for (size_t i = 0; i < N; ++i) {
    mutation_rand_vals[i] = unif_dist(rng);
  }

  tbb::concurrent_vector<Cell> new_cells;
  tbb::concurrent_vector<GlobalDeathEvent> dead_cells;
  std::atomic<uint32_t> new_cells_count(0), death_count(0);

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
            death_count++;
            continue;
          }

          if (birth_probs[static_cast<Eigen::Index>(i)] > tau_step) {
            continue;
          }

          new_cells_count += 2;
          dead_cells.push_back({idx, cell->second.parent_id});
          death_count++;

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
              Cell daughter_cell1 =
                  Cell(cell->second, cell->second.fitness * (1.0 + mut.second.effect));
              daughter_cell1.mutations.push_back({0, mut.second.type_id});
              new_cells.push_back(std::move(daughter_cell1));
              new_cells.emplace_back(cell->second, cell->second.fitness);
              break;
            }
          }
        }
      });

  std::vector<Cell> sorted_new_cells;
  sorted_new_cells.reserve(new_cells.size());
  for (auto& cell : new_cells) {
    sorted_new_cells.push_back(std::move(cell));
  }

  std::sort(sorted_new_cells.begin(), sorted_new_cells.end(), globalDaughterCellLess);

  const uint32_t starting_id = static_cast<uint32_t>(N + total_deaths);
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

  result.deaths.reserve(dead_cells.size());
  for (const auto& death : dead_cells) {
    result.deaths.push_back(death);
  }
  std::sort(result.deaths.begin(), result.deaths.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.id < rhs.id;
  });

  for (const auto& death : result.deaths) {
    cells_graveyard.insert({death.id, {death.parent_id, tau}});
    cells.erase(death.id);
  }

  const size_t created_count = new_cells_count.load();
  const size_t removed_count = death_count.load();
  total_deaths += removed_count;
  actual_population = actual_population + created_count - removed_count;

  return result;
}

}  // namespace CellEvoX::systems
