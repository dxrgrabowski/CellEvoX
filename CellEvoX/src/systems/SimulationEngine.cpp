#include "systems/SimulationEngine.hpp"

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_pipeline.h>
#include <tbb/tbb.h>

#include <Eigen/Dense>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <execution>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <system_error>
#include <unordered_set>

#include "io/PopulationSnapshotIO.hpp"
#include "utils/MathUtils.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif
#include "systems/CommonPopulationStep.hpp"
#include "utils/SimulationConfig.hpp"
#include "utils/PhaseProfiler.hpp"

namespace {

constexpr double kTauSnapshotEpsilon = 1e-9;
constexpr size_t kDenseAppendGrainSize = 2048;
constexpr size_t kDenseSmallEventBatchThreshold = 4096;

int tauSnapshotIndex(double tau_value) {
  return static_cast<int>(std::floor(tau_value + kTauSnapshotEpsilon));
}

bool shouldRebuildDenseAliveCellIds(const std::vector<uint32_t>& alive_cell_ids,
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

std::vector<uint32_t> rebuildDenseAliveCellIds(const std::vector<uint32_t>& alive_cell_ids,
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

}  // namespace

using namespace utils;

std::atomic<bool> SimulationEngine::shutdown_requested{false};

void SimulationEngine::signalHandler(int signum) {
  spdlog::warn("\nReceived interrupt signal ({}). Gracefully shutting down...", signum);
  shutdown_requested.store(true);
}

SimulationEngine::SimulationEngine(std::shared_ptr<SimulationConfig> config)
    : actual_population(config->initial_population),
      total_deaths(0),
      tau(0.0),
      config(config),
      rng(config->seed) {
  
  // Set global spdlog level based on config verbosity
  // 0 = off, 1 = warnings only, 2 = full info/debug
  switch (config->verbosity) {
    case 0: spdlog::set_level(spdlog::level::off); break;
    case 1: spdlog::set_level(spdlog::level::warn); break;
    default: spdlog::set_level(spdlog::level::info); break;
  }

  cells.rehash(config->initial_population);
  alive_cell_indices_cache.reserve(config->initial_population);
  dense_cells.reserve(config->initial_population);
  dense_alive_cell_ids.reserve(config->initial_population);
  dense_cell_slot_by_id.reserve(config->initial_population);
  dense_alive_flags.reserve(config->initial_population);

  for (uint32_t i = 0; i < config->initial_population; ++i) {
    dense_cells.emplace_back(i);
    dense_alive_cell_ids.push_back(i);
    dense_cell_slot_by_id.push_back(i);
    dense_alive_flags.push_back(1);
    alive_cell_indices_cache.push_back(i);
  }

  for (const auto& mutation : config->mutations) {
    available_mutation_types[mutation.type_id] = mutation;
  }

  total_mutation_probability =
      std::accumulate(available_mutation_types.begin(),
                      available_mutation_types.end(),
                      0.0,
                      [](double sum, const std::pair<const uint8_t, MutationType>& pair) {
                        return sum + pair.second.probability;
                      });

  // These are informational logs; they will be filtered by spdlog's level.
  spdlog::info("=== Simulation Engine Initialized ===");
  spdlog::info("Initial population: {}, Capacity: {}", config->initial_population, config->env_capacity);
  spdlog::info("Tau step: {}, Total mutation probability: {:.6f}", config->tau_step, total_mutation_probability);

  // Initialize memory logging
  const auto memory_log_path =
      std::filesystem::path(config->output_path) / "statistics" / "memory_log.csv";

  memory_log_file.open(memory_log_path);
  if (!memory_log_file.is_open()) {
    memory_log_file.clear();
    std::error_code directory_error;
    std::filesystem::create_directories(memory_log_path.parent_path(), directory_error);
    if (directory_error) {
      spdlog::warn("Failed to create memory log directory at: {} ({})",
                   memory_log_path.parent_path().string(),
                   directory_error.message());
    } else {
      memory_log_file.open(memory_log_path);
    }
  }

  if (memory_log_file.is_open()) {
      memory_log_file << "Tau,RSS_KB,Cells_Count,Graveyard_Count,Estimated_Cells_KB,Estimated_Graveyard_KB\n";
  } else {
      spdlog::warn("Failed to open memory log file at: {}", memory_log_path.string());
  }
}

void SimulationEngine::step() {
  switch (config->sim_type) {
    case SimulationType::STOCHASTIC_TAU_LEAP:
      stochasticStep();
      break;
    default:
      break;
      // case SimulationType::DETERMINISTIC_RK4:
      //     deterministicStep();
      //     break;
  }
}

void SimulationEngine::runSteps(uint32_t steps) {
  auto last_update_time = std::chrono::steady_clock::now();
  const char* spinner = "|/-\\";
  int spinner_index = 0;
  const int bar_width = 50;

  auto start_time = std::chrono::steady_clock::now();

  for (uint32_t i = 0; i < steps; ++i) {
    if (shutdown_requested.load()) {
      spdlog::info("Shutdown requested at step {}/{}", i, steps);
      std::cout << std::endl;
      break;
    }
    if (config->max_population_cutoff > 0 &&
        actual_population >= config->max_population_cutoff) {
      spdlog::warn("Population cutoff reached: {} >= {} at tau={:.2f}. Stopping.",
                   actual_population, config->max_population_cutoff, tau);
      std::cout << std::endl;
      break;
    }
    step();
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_update_time)
            .count();
    if (elapsed_time >= 100) {
      int progress = static_cast<int>((static_cast<double>(i + 1) / steps) * 100);
      int pos = static_cast<int>((static_cast<double>(i + 1) / steps) * bar_width);

      auto total_elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
      double avg_time_per_step = static_cast<double>(total_elapsed) / (i + 1);
      int remaining_steps = steps - (i + 1);
      double estimated_remaining_time = remaining_steps * avg_time_per_step / 1000.0;

      std::cout << "\r\033[1;32mProgress: [\033[35m";
      for (int j = 0; j < bar_width; ++j) {
        if (j < pos)
          std::cout << "#";
        else
          std::cout << " ";
      }

      std::cout << "\033[1;32m] " << progress << "% \033[34m" << spinner[spinner_index]
                << " \033[0m" << remaining_steps << " steps remaining, ~" << std::fixed
                << std::setprecision(1) << estimated_remaining_time << "s left "
                << actual_population << " cells" << std::flush;

      spinner_index = (spinner_index + 1) % 4;
      last_update_time = current_time;
    }
  }
  std::cout << "\r\033[1;32mProgress: [";
  for (int j = 0; j < bar_width; ++j) {
    std::cout << "#";
  }
  std::cout << "] 100% \033[0m" << std::endl;
}

ecs::Run SimulationEngine::run(uint32_t steps, bool run_postprocessing) {
  runSteps(steps);

  materializeCellsFromDense();
  materializeGraveyardFromDense();

  return ecs::Run(std::move(cells),
                  std::move(available_mutation_types),
                  std::move(cells_graveyard),
                  std::move(generational_stat_report),
                  std::move(generational_popul_report),
                  total_deaths,
                  tau,
                  run_postprocessing);
}

void SimulationEngine::runSimulationOnly(uint32_t steps) {
  runSteps(steps);
}

void SimulationEngine::stop() { spdlog::info("Simulation stopped"); }

void SimulationEngine::stochasticStep() {
  stochasticDenseStep();
}

void SimulationEngine::stochasticDenseStep() {
  CELLEVOX_PROFILE_PHASE("stochastic_step_total");
  tau += config->tau_step;

  const double tau_step = config->tau_step;
  const size_t Nc = config->env_capacity;

  if (shouldRebuildDenseAliveCellIds(dense_alive_cell_ids, actual_population)) {
    CELLEVOX_PROFILE_PHASE("rebuild_dense_alive_ids");
    dense_alive_cell_ids =
        rebuildDenseAliveCellIds(dense_alive_cell_ids, dense_alive_flags, actual_population);
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
              const double death_draw = CellEvoX::deterministic_rng::uniform01(
                  config->seed, rng_step, idx, 0);
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
              const double birth_draw = CellEvoX::deterministic_rng::uniform01(
                  config->seed, rng_step, idx, 1);
              if (birth_draw > birth_event_threshold) {
                continue;
              }

              buffers.dead_cells.push_back({idx, cell.parent_id});

              const double rand_val = CellEvoX::deterministic_rng::uniform01(
                  config->seed, rng_step, idx, 2);
              if (rand_val >= total_mutation_probability) {
                buffers.new_cells.emplace_back(cell, cell.fitness);
                buffers.new_cells.emplace_back(cell, cell.fitness);
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
                  Cell daughter_cell1 = Cell(cell, daughter_fitness);
                  daughter_cell1.mutations.push_back({0, mut.second.type_id});
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
      if (new_cell_count < kDenseSmallEventBatchThreshold) {
        for (size_t buffer_index = 0; buffer_index < buffer_ptrs.size(); ++buffer_index) {
          copy_birth_buffer(buffer_index);
        }
      } else {
        tbb::parallel_for(size_t{0}, buffer_ptrs.size(), copy_birth_buffer);
      }
      CellEvoX::parallel_algorithms::sortMaybeParallel(
          sorted_new_cells.begin(),
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
          dense_alive_flags[death.id] = 0;
          dense_pending_graveyard_entries[old_pending_graveyard_size + target] =
              {death.id, {death.parent_id, tau}};
          dense_free_slots[old_free_slots_size + target] = dense_cell_slot_by_id[death.id];
        }
      };
      if (dead_cell_count < kDenseSmallEventBatchThreshold) {
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
        const uint32_t slot =
            i < reusable_count
                ? dense_free_slots[old_free_slots_size - 1 - i]
                : static_cast<uint32_t>(old_dense_size + (i - reusable_count));
        auto& new_cell = sorted_new_cells[i];
        new_cell.id = new_id;
        for (auto& mutation : new_cell.mutations) {
          if (mutation.first == 0) {
            mutation.first = new_id;
          }
        }
        dense_cells[slot] = std::move(new_cell);
        dense_cell_slot_by_id[old_id_count + i] = slot;
        dense_alive_flags[old_id_count + i] = 1;
        dense_alive_cell_ids[old_alive_ids_size + i] = new_id;
      };
      if (birth_count < kDenseSmallEventBatchThreshold) {
        for (size_t i = 0; i < birth_count; ++i) {
          append_birth(i);
        }
      } else {
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, birth_count, kDenseAppendGrainSize),
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
    cells_dirty_from_dense = true;
  }

  const int current_tau = tauSnapshotIndex(tau);
  if (config->stat_res > 0 && current_tau % config->stat_res == 0 &&
      current_tau != last_stat_snapshot_tau) {
    CELLEVOX_PROFILE_PHASE("stat_snapshot");
    takeStatSnapshot();
    last_stat_snapshot_tau = current_tau;
  }
  if (config->popul_res > 0 && current_tau % config->popul_res == 0 &&
      current_tau != last_population_snapshot_tau) {
    CELLEVOX_PROFILE_PHASE("population_snapshot");
    takePopulationSnapshot();
    last_population_snapshot_tau = current_tau;
  }

  if (config->graveyard_pruning_interval > 0 &&
      current_tau > 0 &&
      current_tau % config->graveyard_pruning_interval == 0 &&
      current_tau != last_pruning_tau) {
      CELLEVOX_PROFILE_PHASE("graveyard_pruning");
      pruneGraveyard();
      last_pruning_tau = current_tau;
  }

  if (config->stat_res > 0 && current_tau % config->stat_res == 0 &&
      current_tau != last_memory_log_tau) {
    CELLEVOX_PROFILE_PHASE("memory_log");
    logMemoryUsage();
    last_memory_log_tau = current_tau;
  }
}

void SimulationEngine::takeStatSnapshot() {
  double total_fitness = 0.0;
  double total_fitness_squared = 0.0;
  double total_fitness_cubed = 0.0;
  double total_fitness_fourth = 0.0;

  double total_mutations = 0.0;
  double total_mutations_squared = 0.0;
  double total_mutations_cubed = 0.0;
  double total_mutations_fourth = 0.0;

  size_t living_cells_count = 0;
  for (uint32_t id : dense_alive_cell_ids) {
    if (id >= dense_alive_flags.size() || dense_alive_flags[id] == 0) {
      continue;
    }
    const auto& cell_val = dense_cells[dense_cell_slot_by_id[id]];
    ++living_cells_count;

    double f = cell_val.fitness;
    double f2 = f * f;
    double f3 = f2 * f;
    double f4 = f3 * f;

    double m = static_cast<double>(cell_val.mutations.size());
    double m2 = m * m;
    double m3 = m2 * m;
    double m4 = m3 * m;

    total_fitness += f;
    total_fitness_squared += f2;
    total_fitness_cubed += f3;
    total_fitness_fourth += f4;

    total_mutations += m;
    total_mutations_squared += m2;
    total_mutations_cubed += m3;
    total_mutations_fourth += m4;
  }

  if (living_cells_count == 0) {
    generational_stat_report.push_back(
        {tau, 0.0, 0.0, 0.0, 0.0, 0, 0.0, 0.0, 0.0, 0.0});
    return;
  }

  double mean_fitness = total_fitness / living_cells_count;
  double mean_mutations = total_mutations / living_cells_count;

  double M2_fitness = total_fitness_squared / living_cells_count;
  double M3_fitness = total_fitness_cubed / living_cells_count;
  double M4_fitness = total_fitness_fourth / living_cells_count;

  double M2_mutations = total_mutations_squared / living_cells_count;
  double M3_mutations = total_mutations_cubed / living_cells_count;
  double M4_mutations = total_mutations_fourth / living_cells_count;

  double fitness_variance = M2_fitness - mean_fitness * mean_fitness;
  double mutations_variance = M2_mutations - mean_mutations * mean_mutations;

  double fitness_skewness =
      M3_fitness - 3.0 * mean_fitness * M2_fitness + 2.0 * std::pow(mean_fitness, 3);
  double fitness_kurtosis = M4_fitness - 4.0 * mean_fitness * M3_fitness +
                            6.0 * mean_fitness * mean_fitness * M2_fitness -
                            3.0 * std::pow(mean_fitness, 4);

  double mutations_skewness =
      M3_mutations - 3.0 * mean_mutations * M2_mutations + 2.0 * std::pow(mean_mutations, 3);
  double mutations_kurtosis = M4_mutations - 4.0 * mean_mutations * M3_mutations +
                              6.0 * mean_mutations * mean_mutations * M2_mutations -
                              3.0 * std::pow(mean_mutations, 4);

  generational_stat_report.push_back({
      tau,
      mean_fitness,
      fitness_variance,
      mean_mutations,
      mutations_variance,
      living_cells_count,
      fitness_skewness,
      fitness_kurtosis,
      mutations_skewness,
      mutations_kurtosis,
  });
}

void SimulationEngine::takePopulationSnapshot() {
  std::vector<CellEvoX::io::PopulationSnapshotRecord> snapshot_records;
  std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutation_payload;
  snapshot_records.reserve(actual_population);
  const auto payload_kind = config->full_mutation_payload
                                ? CellEvoX::io::MutationPayloadKind::Full
                                : CellEvoX::io::MutationPayloadKind::DriverOnly;

  CellMap cells_copy;
  cells_copy.rehash(actual_population);
  for (uint32_t id : dense_alive_cell_ids) {
    if (id >= dense_alive_flags.size() || dense_alive_flags[id] == 0) {
      continue;
    }
    const auto& cell = dense_cells[dense_cell_slot_by_id[id]];
    CellMap::accessor accessor;
    cells_copy.insert(accessor, {cell.id, cell});

    if (mutation_payload.size() > std::numeric_limits<uint32_t>::max()) {
      spdlog::error("Population snapshot mutation payload exceeds uint32_t offset space");
      return;
    }
    const uint32_t mutation_payload_offset = static_cast<uint32_t>(mutation_payload.size());
    for (const auto& [mutation_id, mutation_type] : cell.mutations) {
      const auto type_it = available_mutation_types.find(mutation_type);
      if (config->full_mutation_payload ||
          (type_it != available_mutation_types.end() && type_it->second.is_driver)) {
        mutation_payload.push_back({mutation_id, mutation_type});
      }
    }
    const auto mutation_payload_count =
        static_cast<uint16_t>(std::min<size_t>(mutation_payload.size() - mutation_payload_offset,
                                               std::numeric_limits<uint16_t>::max()));

    snapshot_records.push_back(
        {cell.id,
         cell.parent_id,
         cell.fitness,
         std::numeric_limits<float>::quiet_NaN(),
         std::numeric_limits<float>::quiet_NaN(),
         std::numeric_limits<float>::quiet_NaN(),
         static_cast<uint16_t>(
             std::min<size_t>(cell.mutations.size(), std::numeric_limits<uint16_t>::max())),
         mutation_payload_count,
         mutation_payload_offset,
         0,
         {0, 0, 0}});
  }

  const auto snapshot_path =
      CellEvoX::io::populationSnapshotPath(config->output_path, tauSnapshotIndex(tau));
  if (!CellEvoX::io::writePopulationSnapshot(
          snapshot_path, tau, 0, snapshot_records, mutation_payload, payload_kind)) {
    spdlog::error("Failed to write population snapshot file: {}", snapshot_path);
  }

  generational_popul_report.push_back({tauSnapshotIndex(tau), std::move(cells_copy)});
}

void SimulationEngine::pruneGraveyard() {
  materializeGraveyardFromDense();
  spdlog::info("Pruning graveyard... Current size: {}", cells_graveyard.size());

  std::unordered_set<uint32_t> living_ids;
  living_ids.reserve(actual_population);
  for (uint32_t id : dense_alive_cell_ids) {
    if (id < dense_alive_flags.size() && dense_alive_flags[id] != 0) {
      living_ids.insert(id);
    }
  }

  std::unordered_set<uint32_t> reachable_dead_cells;
  reachable_dead_cells.reserve(cells_graveyard.size());

  for (uint32_t id : dense_alive_cell_ids) {
    if (id >= dense_alive_flags.size() || dense_alive_flags[id] == 0) {
      continue;
    }
    const auto& cell = dense_cells[dense_cell_slot_by_id[id]];
    uint32_t parent_id = cell.parent_id;
    while (parent_id != 0) {
      if (reachable_dead_cells.count(parent_id) || living_ids.count(parent_id)) {
        break;
      }

      Graveyard::const_accessor grave_accessor;
      if (cells_graveyard.find(grave_accessor, parent_id)) {
        reachable_dead_cells.insert(parent_id);
        parent_id = grave_accessor->second.first;
      } else {
        break;
      }
    }
  }

  std::vector<uint32_t> to_remove;
  for (const auto& item : cells_graveyard) {
      if (reachable_dead_cells.find(item.first) == reachable_dead_cells.end()) {
          to_remove.push_back(item.first);
      }
  }

  for (uint32_t id : to_remove) {
      cells_graveyard.erase(id);
  }

  spdlog::info("Graveyard pruned. New size: {}. Removed: {} cells.",
               cells_graveyard.size(), to_remove.size());
}

void SimulationEngine::materializeCellsFromDense() {
  if (!cells_dirty_from_dense && cells.size() == actual_population) {
    return;
  }

  CELLEVOX_PROFILE_PHASE("materialize_cells_from_dense");
  cells.clear();
  cells.rehash(actual_population);
  alive_cell_indices_cache.clear();
  alive_cell_indices_cache.reserve(actual_population);

  for (uint32_t id : dense_alive_cell_ids) {
    if (id >= dense_alive_flags.size() || dense_alive_flags[id] == 0) {
      continue;
    }
    const auto& cell = dense_cells[dense_cell_slot_by_id[id]];
    cells.insert({cell.id, cell});
    alive_cell_indices_cache.push_back(cell.id);
  }

  actual_population = alive_cell_indices_cache.size();
  cells_dirty_from_dense = false;
}

void SimulationEngine::materializeGraveyardFromDense() {
  if (dense_pending_graveyard_entries.empty()) {
    return;
  }

  CELLEVOX_PROFILE_PHASE("materialize_graveyard_from_dense");
  cells_graveyard.rehash(cells_graveyard.size() + dense_pending_graveyard_entries.size());
  tbb::parallel_for(
      tbb::blocked_range<size_t>(0, dense_pending_graveyard_entries.size()),
      [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i != range.end(); ++i) {
          const auto& entry = dense_pending_graveyard_entries[i];
          cells_graveyard.insert({entry.first, entry.second});
        }
      });
  dense_pending_graveyard_entries.clear();
}

size_t SimulationEngine::getRSS() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        return static_cast<size_t>(counters.WorkingSetSize / 1024);
    }
    return 0;
#else
    size_t rss = 0;
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        size_t ignore;
        statm >> ignore >> rss; // 2nd value is RSS in pages
    }
    long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
    return rss * page_size_kb;
#endif
}

void SimulationEngine::logMemoryUsage() {
    if (!memory_log_file.is_open()) return;

    size_t rss_kb = getRSS();
    size_t cells_count = actual_population;
    size_t graveyard_count = cells_graveyard.size() + dense_pending_graveyard_entries.size();
    
    // Estimations
    size_t estimated_cells_kb = (cells_count * sizeof(Cell)) / 1024;
    // Graveyard value is pair<uint32_t, double> (12 bytes) + key (4 bytes) + overhead (~16-24 bytes node)
    // bucket overhead etc. TBB map is complex. 
    // Approx 32-48 bytes per entry?
    size_t estimated_graveyard_kb = (graveyard_count * 48) / 1024; 

    memory_log_file << tau << "," 
                    << rss_kb << "," 
                    << cells_count << "," 
                    << graveyard_count << ","
                    << estimated_cells_kb << ","
                    << estimated_graveyard_kb << "\n";
}
