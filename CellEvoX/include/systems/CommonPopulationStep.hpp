#pragma once

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "systems/DensePopulation.hpp"
#include "systems/MutationEffect.hpp"
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

namespace detail {

constexpr size_t kDenseAppendGrainSize = 2048;
constexpr size_t kDenseSmallEventBatchThreshold = 4096;

inline bool shouldRebuildDenseAliveCellIds(const std::vector<uint32_t>& alive_cell_ids,
                                           size_t actual_population) {
  if (actual_population == 0) {
    return !alive_cell_ids.empty();
  }
  if (alive_cell_ids.empty() || alive_cell_ids.size() < actual_population) {
    return true;
  }

  const size_t stale_slack = std::max<size_t>(8192, actual_population / 4);
  return alive_cell_ids.size() > actual_population + stale_slack;
}

inline std::vector<uint32_t> rebuildDenseAliveCellIds(const std::vector<uint32_t>& alive_cell_ids,
                                                      const std::vector<uint8_t>& alive_flags,
                                                      size_t actual_population) {
  constexpr size_t chunk_size = 16384;
  const size_t chunk_count =
      alive_cell_ids.empty() ? 0 : (alive_cell_ids.size() + chunk_size - 1) / chunk_size;
  std::vector<std::vector<uint32_t>> chunks(chunk_count);

  tbb::parallel_for(size_t{0}, chunk_count, [&](size_t chunk_index) {
    const size_t begin = chunk_index * chunk_size;
    const size_t end = std::min(begin + chunk_size, alive_cell_ids.size());
    auto& chunk = chunks[chunk_index];
    chunk.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) {
      const uint32_t id = alive_cell_ids[i];
      if (id < alive_flags.size() && alive_flags[id] != 0) {
        chunk.push_back(id);
      }
    }
  });

  std::vector<uint32_t> rebuilt_alive_ids;
  rebuilt_alive_ids.reserve(actual_population);
  for (auto& chunk : chunks) {
    rebuilt_alive_ids.insert(rebuilt_alive_ids.end(), chunk.begin(), chunk.end());
  }
  return rebuilt_alive_ids;
}

}  // namespace detail

inline bool commonDaughterCellLess(const Cell& lhs, const Cell& rhs) {
  if (lhs.parent_id != rhs.parent_id) return lhs.parent_id < rhs.parent_id;
  if (lhs.fitness != rhs.fitness) return lhs.fitness < rhs.fitness;
  if (lhs.mutations.size() != rhs.mutations.size()) {
    return lhs.mutations.size() < rhs.mutations.size();
  }
  if (!lhs.mutations.empty() && !rhs.mutations.empty()) {
    return lhs.mutations.back().mutation_type < rhs.mutations.back().mutation_type;
  }
  return false;
}

// The base caller compiles out event lists; xyz requests them for its overlay.
template <bool collect_event_result = true>
inline CommonPopulationStepResult applyCommonPopulationStep(
    DensePopulation& population,
    const SimulationConfig& config,
    const std::map<uint8_t, MutationType>& available_mutation_types,
    double total_mutation_probability,
    size_t& actual_population,
    size_t& total_deaths,
    double tau) {
  CommonPopulationStepResult result;
  auto& dense_cells = population.cells;
  auto& dense_alive_cell_ids = population.alive_cell_ids;
  auto& dense_cell_slot_by_id = population.cell_slot_by_id;
  auto& dense_alive_flags = population.alive_flags;
  auto& dense_free_slots = population.free_slots;
  auto& dense_pending_graveyard_entries = population.pending_graveyard_entries;

  const double tau_step = config.tau_step;
  const size_t Nc = config.env_capacity;

  if (detail::shouldRebuildDenseAliveCellIds(dense_alive_cell_ids, actual_population)) {
    CELLEVOX_PROFILE_PHASE("rebuild_dense_alive_ids");
    dense_alive_cell_ids = detail::rebuildDenseAliveCellIds(
        dense_alive_cell_ids, dense_alive_flags, actual_population);
    actual_population = dense_alive_cell_ids.size();
  }

  if (Nc > 0 && actual_population > 0) {
    if (!std::isfinite(total_mutation_probability) || total_mutation_probability < 0.0 ||
        total_mutation_probability > 1.0) {
      throw std::invalid_argument("total_mutation_probability must be finite and in [0, 1]");
    }

    const size_t N = actual_population;
    const double scaling_factor = static_cast<double>(N) / static_cast<double>(Nc);
    const double death_event_threshold = -std::expm1(-tau_step * scaling_factor);
    const uint64_t rng_step =
        tau_step > 0.0 ? static_cast<uint64_t>(std::llround(tau / tau_step)) : 0ULL;

    tbb::enumerable_thread_specific<CellEvoX::systems::CommonPopulationThreadBuffers>
        thread_buffers;

    {
      CELLEVOX_PROFILE_PHASE("parallel_events");
      tbb::parallel_for(
          tbb::blocked_range<size_t>(0, dense_alive_cell_ids.size()),
          [&](const tbb::blocked_range<size_t>& range) {
            auto& buffers = thread_buffers.local();
            for (size_t i = range.begin(); i != range.end(); ++i) {
              const uint32_t idx = dense_alive_cell_ids[i];
              if (idx >= dense_alive_flags.size() || dense_alive_flags[idx] == 0) {
                continue;
              }

              const Cell& cell = dense_cells[dense_cell_slot_by_id[idx]];
              const double death_draw =
                  CellEvoX::deterministic_rng::uniform01(config.seed, rng_step, idx, 0);
              if (death_draw <= death_event_threshold) {
                buffers.dead_cells.push_back({idx, cell.parent_id});
                continue;
              }

              const double fitness = cell.fitness;
              if (!std::isfinite(fitness) || fitness <= 0.0) {
                spdlog::error("Cell {} has invalid fitness {}; skipping birth event", idx, fitness);
                continue;
              }

              const double birth_event_threshold = -std::expm1(-tau_step * fitness);
              const double birth_draw =
                  CellEvoX::deterministic_rng::uniform01(config.seed, rng_step, idx, 1);
              if (birth_draw > birth_event_threshold) {
                continue;
              }

              buffers.dead_cells.push_back({idx, cell.parent_id});

              const double rand_val =
                  CellEvoX::deterministic_rng::uniform01(config.seed, rng_step, idx, 2);
              if (rand_val >= total_mutation_probability) {
                buffers.new_cells.emplace_back(cell, cell.fitness);
                buffers.new_cells.emplace_back(cell, cell.fitness);
                continue;
              }

              double prob_sum = 0.0;
              for (const auto& mut : available_mutation_types) {
                prob_sum += mut.second.probability;
                if (rand_val < prob_sum) {
                  const float realized_effect =
                      detail::realizedMutationEffect(mut.second, config.seed, rng_step, idx, 0);
                  const double daughter_fitness = fitness * (1.0 + realized_effect);
                  if (!std::isfinite(daughter_fitness) || daughter_fitness <= 0.0) {
                    throw std::runtime_error("Mutation produced invalid daughter fitness");
                  }
                  Cell daughter_cell1 = Cell(cell, daughter_fitness);
                  daughter_cell1.mutations.push_back(
                      {0, mut.second.type_id, realized_effect});
                  buffers.new_cells.push_back(std::move(daughter_cell1));
                  buffers.new_cells.emplace_back(cell, fitness);
                  break;
                }
              }
            }
          });
    }

    std::vector<CellEvoX::systems::CommonPopulationThreadBuffers*> buffer_ptrs;
    size_t new_cell_count = 0;
    size_t dead_cell_count = 0;
    for (auto& buffers : thread_buffers) {
      buffer_ptrs.push_back(&buffers);
      new_cell_count += buffers.new_cells.size();
      dead_cell_count += buffers.dead_cells.size();
    }

    std::vector<size_t> birth_offsets(buffer_ptrs.size() + 1, 0);
    std::vector<size_t> death_offsets(buffer_ptrs.size() + 1, 0);
    for (size_t i = 0; i < buffer_ptrs.size(); ++i) {
      birth_offsets[i + 1] = birth_offsets[i] + buffer_ptrs[i]->new_cells.size();
      death_offsets[i + 1] = death_offsets[i] + buffer_ptrs[i]->dead_cells.size();
    }

    std::vector<Cell> sorted_new_cells;
    {
      CELLEVOX_PROFILE_PHASE("copy_sort_births");
      sorted_new_cells.resize(new_cell_count);
      const auto copy_birth_buffer = [&](size_t buffer_index) {
        auto& new_cells = buffer_ptrs[buffer_index]->new_cells;
        const size_t offset = birth_offsets[buffer_index];
        for (size_t i = 0; i < new_cells.size(); ++i) {
          sorted_new_cells[offset + i] = std::move(new_cells[i]);
        }
      };
      if (new_cell_count < detail::kDenseSmallEventBatchThreshold) {
        for (size_t buffer_index = 0; buffer_index < buffer_ptrs.size(); ++buffer_index) {
          copy_birth_buffer(buffer_index);
        }
      } else {
        tbb::parallel_for(size_t{0}, buffer_ptrs.size(), copy_birth_buffer);
      }
      CellEvoX::parallel_algorithms::sortMaybeParallel(sorted_new_cells.begin(),
                                                       sorted_new_cells.end(),
                                                       CellEvoX::systems::commonDaughterCellLess);
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
    if (dense_cell_slot_by_id.size() != starting_id || dense_alive_flags.size() != starting_id) {
      throw std::runtime_error("Dense stochastic storage is out of sync with cell ids");
    }

    {
      CELLEVOX_PROFILE_PHASE("apply_deaths");
      if constexpr (collect_event_result) {
        result.deaths.resize(dead_cell_count);
      }
      const size_t old_pending_graveyard_size = dense_pending_graveyard_entries.size();
      const size_t old_free_slots_size = dense_free_slots.size();
      dense_pending_graveyard_entries.resize(old_pending_graveyard_size + dead_cell_count);
      dense_free_slots.resize(old_free_slots_size + dead_cell_count);
      const auto apply_death_buffer = [&](size_t buffer_index) {
        const auto& dead_cells = buffer_ptrs[buffer_index]->dead_cells;
        const size_t offset = death_offsets[buffer_index];
        for (size_t i = 0; i < dead_cells.size(); ++i) {
          const auto& death = dead_cells[i];
          const size_t target = offset + i;
          if constexpr (collect_event_result) {
            result.deaths[target] = death;
          }
          dense_alive_flags[death.id] = 0;
          dense_pending_graveyard_entries[old_pending_graveyard_size + target] = {
              death.id, {death.parent_id, tau}};
          dense_free_slots[old_free_slots_size + target] = dense_cell_slot_by_id[death.id];
        }
      };
      if (dead_cell_count < detail::kDenseSmallEventBatchThreshold) {
        for (size_t buffer_index = 0; buffer_index < buffer_ptrs.size(); ++buffer_index) {
          apply_death_buffer(buffer_index);
        }
      } else {
        tbb::parallel_for(size_t{0}, buffer_ptrs.size(), apply_death_buffer);
      }
    }

    {
      CELLEVOX_PROFILE_PHASE("append_births");
      const size_t birth_count = sorted_new_cells.size();
      if constexpr (collect_event_result) {
        result.births.resize(birth_count);
      }
      const size_t old_alive_ids_size = dense_alive_cell_ids.size();
      const size_t old_id_count = dense_cell_slot_by_id.size();
      const size_t old_dense_size = dense_cells.size();
      const size_t old_free_slots_size = dense_free_slots.size();
      const size_t reusable_count = std::min(old_free_slots_size, birth_count);
      const size_t extra_count = birth_count - reusable_count;

      dense_cells.resize(old_dense_size + extra_count);
      dense_alive_cell_ids.resize(old_alive_ids_size + birth_count);
      dense_cell_slot_by_id.resize(old_id_count + birth_count);
      dense_alive_flags.resize(old_id_count + birth_count);

      const auto append_birth = [&](size_t i) {
        const uint32_t new_id = starting_id + static_cast<uint32_t>(i);
        const uint32_t slot = i < reusable_count
                                  ? dense_free_slots[old_free_slots_size - 1 - i]
                                  : static_cast<uint32_t>(old_dense_size + (i - reusable_count));
        auto& new_cell = sorted_new_cells[i];
        new_cell.id = new_id;
        for (auto& mutation : new_cell.mutations) {
          if (mutation.mutation_id == 0) {
            mutation.mutation_id = new_id;
          }
        }
        if constexpr (collect_event_result) {
          result.births[i] = {new_id, new_cell.parent_id};
        }
        dense_cells[slot] = std::move(new_cell);
        dense_cell_slot_by_id[old_id_count + i] = slot;
        dense_alive_flags[old_id_count + i] = 1;
        dense_alive_cell_ids[old_alive_ids_size + i] = new_id;
      };
      if (birth_count < detail::kDenseSmallEventBatchThreshold) {
        for (size_t i = 0; i < birth_count; ++i) {
          append_birth(i);
        }
      } else {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, birth_count, detail::kDenseAppendGrainSize),
                          [&](const tbb::blocked_range<size_t>& range) {
                            for (size_t i = range.begin(); i != range.end(); ++i) {
                              append_birth(i);
                            }
                          });
      }
      dense_free_slots.resize(old_free_slots_size - reusable_count);
    }

    if (dead_cell_count > static_cast<size_t>(max_cell_id) - total_deaths) {
      throw std::overflow_error("Cell death counter exceeds uint32_t cell id space");
    }
    total_deaths += dead_cell_count;
    actual_population = N - dead_cell_count + sorted_new_cells.size();
  }

  if constexpr (collect_event_result) {
    CellEvoX::parallel_algorithms::sortMaybeParallel(
        result.deaths.begin(), result.deaths.end(), [](const auto& lhs, const auto& rhs) {
          return lhs.id < rhs.id;
        });
  }
  return result;
}

}  // namespace CellEvoX::systems
