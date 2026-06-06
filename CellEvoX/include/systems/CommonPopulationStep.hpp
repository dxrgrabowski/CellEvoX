#pragma once

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "systems/SimulationEngine.hpp"
#include "utils/DeterministicRng.hpp"
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

struct CommonPopulationThreadBuffers {
  std::vector<Cell> new_cells;
  std::vector<CommonDeathEvent> dead_cells;
};

inline void rebuildCachedAliveCellIndices(std::vector<uint32_t>& alive_cell_indices,
                                          const CellMap& cells) {
  alive_cell_indices.clear();
  alive_cell_indices.reserve(cells.size());
  for (const auto& cell : cells) {
    alive_cell_indices.push_back(cell.first);
  }
  CellEvoX::parallel_algorithms::sortMaybeParallel(alive_cell_indices.begin(),
                                                   alive_cell_indices.end());
}

inline bool shouldRebuildCachedAliveCellIndices(const std::vector<uint32_t>& alive_cell_indices,
                                                size_t actual_population) {
  if (alive_cell_indices.empty() || alive_cell_indices.size() < actual_population) {
    return true;
  }

  const size_t stale_slack = std::max<size_t>(8192, actual_population / 20);
  return alive_cell_indices.size() > actual_population + stale_slack;
}

inline void appendCachedAliveCellBirths(std::vector<uint32_t>& alive_cell_indices,
                                        const std::vector<CommonBirthEvent>& births) {
  if (births.empty()) {
    return;
  }

  alive_cell_indices.reserve(alive_cell_indices.size() + births.size());
  for (const auto& birth : births) {
    alive_cell_indices.push_back(birth.id);
  }
}

inline void appendCachedAliveCellBirthRange(std::vector<uint32_t>& alive_cell_indices,
                                            uint32_t starting_id,
                                            size_t birth_count) {
  if (birth_count == 0) {
    return;
  }

  alive_cell_indices.reserve(alive_cell_indices.size() + birth_count);
  for (size_t i = 0; i < birth_count; ++i) {
    alive_cell_indices.push_back(starting_id + static_cast<uint32_t>(i));
  }
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
    std::mt19937& rng,
    std::vector<uint32_t>* cached_alive_cell_indices = nullptr,
    bool collect_event_result = true) {
  CELLEVOX_PROFILE_PHASE("common_step_total");
  CommonPopulationStepResult result;

  const double tau_step = config.tau_step;
  const size_t Nc = config.env_capacity;
  if (Nc == 0 || actual_population == 0) {
    actual_population = cells.size();
    return result;
  }
  if (!std::isfinite(total_mutation_probability) || total_mutation_probability < 0.0 ||
      total_mutation_probability > 1.0) {
    throw std::invalid_argument("total_mutation_probability must be finite and in [0, 1]");
  }

  std::vector<uint32_t> collected_alive_cell_indices;
  std::vector<uint32_t>* alive_cell_indices_ptr = cached_alive_cell_indices;
  if (cached_alive_cell_indices != nullptr) {
    if (shouldRebuildCachedAliveCellIndices(*cached_alive_cell_indices, actual_population)) {
      CELLEVOX_PROFILE_PHASE("rebuild_alive_cache");
      rebuildCachedAliveCellIndices(*cached_alive_cell_indices, cells);
      actual_population = cells.size();
    }
  } else {
    {
      CELLEVOX_PROFILE_PHASE("collect_alive_ids");
      collected_alive_cell_indices.reserve(cells.size());
      for (auto it = cells.begin(); it != cells.end(); ++it) {
        collected_alive_cell_indices.push_back(it->first);
      }
    }

    {
      CELLEVOX_PROFILE_PHASE("sort_alive_ids");
      CellEvoX::parallel_algorithms::sortMaybeParallel(collected_alive_cell_indices.begin(),
                                                       collected_alive_cell_indices.end());
    }
    alive_cell_indices_ptr = &collected_alive_cell_indices;
  }

  auto& alive_cell_indices = *alive_cell_indices_ptr;
  if (alive_cell_indices.empty() || actual_population == 0) {
    actual_population = 0;
    return result;
  }

  if (cached_alive_cell_indices == nullptr && alive_cell_indices.size() != actual_population) {
    spdlog::error("Mismatch in alive cell count: expected {}, found {}",
                  actual_population,
                  alive_cell_indices.size());
    actual_population = alive_cell_indices.size();
  }

  const size_t N = actual_population;
  const double scaling_factor = static_cast<double>(N) / static_cast<double>(Nc);
  const double death_event_threshold = -std::expm1(-tau_step * scaling_factor);
  const uint64_t rng_step =
      tau_step > 0.0 ? static_cast<uint64_t>(std::llround(tau / tau_step)) : 0ULL;
  (void)rng;

  tbb::enumerable_thread_specific<CommonPopulationThreadBuffers> thread_buffers;

  {
    CELLEVOX_PROFILE_PHASE("parallel_events");
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, alive_cell_indices.size()),
        [&](const tbb::blocked_range<size_t>& range) {
          auto& buffers = thread_buffers.local();
          for (size_t i = range.begin(); i != range.end(); ++i) {
            const uint32_t idx = alive_cell_indices[i];
            CellMap::const_accessor cell;
            if (!cells.find(cell, idx)) {
              continue;
            }

            const double death_draw = CellEvoX::deterministic_rng::uniform01(
                config.seed, rng_step, idx, 0);
            if (death_draw <= death_event_threshold) {
              buffers.dead_cells.push_back({idx, cell->second.parent_id});
              continue;
            }

            const double fitness = cell->second.fitness;
            if (!std::isfinite(fitness) || fitness <= 0.0) {
              spdlog::error("Cell {} has invalid fitness {}; skipping birth event", idx, fitness);
              continue;
            }

            const double birth_event_threshold = -std::expm1(-tau_step * fitness);
            const double birth_draw = CellEvoX::deterministic_rng::uniform01(
                config.seed, rng_step, idx, 1);
            if (birth_draw > birth_event_threshold) {
              continue;
            }

            buffers.dead_cells.push_back({idx, cell->second.parent_id});

            const double rand_val = CellEvoX::deterministic_rng::uniform01(
                config.seed, rng_step, idx, 2);
            if (rand_val >= total_mutation_probability) {
              buffers.new_cells.emplace_back(cell->second, cell->second.fitness);
              buffers.new_cells.emplace_back(cell->second, cell->second.fitness);
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
                Cell daughter_cell1 = Cell(cell->second, daughter_fitness);
                daughter_cell1.mutations.push_back({0, mut.second.type_id});
                buffers.new_cells.push_back(std::move(daughter_cell1));
                buffers.new_cells.emplace_back(cell->second, fitness);
                break;
              }
            }
          }
        });
  }

  size_t new_cell_count = 0;
  size_t dead_cell_count = 0;
  for (const auto& buffers : thread_buffers) {
    new_cell_count += buffers.new_cells.size();
    dead_cell_count += buffers.dead_cells.size();
  }

  std::vector<Cell> sorted_new_cells;
  {
    CELLEVOX_PROFILE_PHASE("copy_sort_births");
    sorted_new_cells.reserve(new_cell_count);
    for (auto& buffers : thread_buffers) {
      for (auto& cell : buffers.new_cells) {
        sorted_new_cells.push_back(std::move(cell));
      }
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
    if (collect_event_result) {
      result.births.resize(sorted_new_cells.size());
    }
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, sorted_new_cells.size()),
        [&](const tbb::blocked_range<size_t>& range) {
          for (size_t i = range.begin(); i != range.end(); ++i) {
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

            if (collect_event_result) {
              result.births[i] = {new_id, accessor->second.parent_id};
            }
          }
        });
  }

  std::vector<CommonDeathEvent> deaths_to_apply;
  {
    CELLEVOX_PROFILE_PHASE("collect_deaths");
    deaths_to_apply.reserve(dead_cell_count);
    for (const auto& buffers : thread_buffers) {
      deaths_to_apply.insert(
          deaths_to_apply.end(), buffers.dead_cells.begin(), buffers.dead_cells.end());
    }
  }

  if (collect_event_result) {
    CELLEVOX_PROFILE_PHASE("sort_deaths");
    result.deaths = std::move(deaths_to_apply);
    CellEvoX::parallel_algorithms::sortMaybeParallel(
        result.deaths.begin(), result.deaths.end(), [](const auto& lhs, const auto& rhs) {
          return lhs.id < rhs.id;
        });
  }

  const auto& deaths_to_erase = collect_event_result ? result.deaths : deaths_to_apply;
  {
    CELLEVOX_PROFILE_PHASE("apply_deaths");
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, deaths_to_erase.size()),
        [&](const tbb::blocked_range<size_t>& range) {
          for (size_t i = range.begin(); i != range.end(); ++i) {
            const auto& death = deaths_to_erase[i];
            cells_graveyard.insert({death.id, {death.parent_id, tau}});
            cells.erase(death.id);
          }
        });
  }

  const size_t removed_count = dead_cell_count;
  if (removed_count > static_cast<size_t>(max_cell_id) - total_deaths) {
    throw std::overflow_error("Cell death counter exceeds uint32_t cell id space");
  }
  total_deaths += removed_count;
  actual_population = N - removed_count + sorted_new_cells.size();

  if (cached_alive_cell_indices != nullptr) {
    CELLEVOX_PROFILE_PHASE("append_alive_cache_births");
    if (collect_event_result) {
      appendCachedAliveCellBirths(*cached_alive_cell_indices, result.births);
    } else {
      appendCachedAliveCellBirthRange(
          *cached_alive_cell_indices, starting_id, sorted_new_cells.size());
    }
  }

  return result;
}

}  // namespace CellEvoX::systems
