#include "systems/SimulationEngine.hpp"

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_pipeline.h>
#include <tbb/tbb.h>

#include <Eigen/Dense>
#include <chrono>
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

using namespace utils;

std::atomic<bool> SimulationEngine::shutdown_requested{false};

void SimulationEngine::signalHandler(int signum) {
  spdlog::warn("\nReceived interrupt signal ({}). Gracefully shutting down...", signum);
  shutdown_requested.store(true);
}

SimulationEngine::SimulationEngine(std::shared_ptr<SimulationConfig> config)
    : tau(0.0), config(config), actual_population(config->initial_population), total_deaths(0), rng(config->seed) {
  
  // Set global spdlog level based on config verbosity
  // 0 = off, 1 = warnings only, 2 = full info/debug
  switch (config->verbosity) {
    case 0: spdlog::set_level(spdlog::level::off); break;
    case 1: spdlog::set_level(spdlog::level::warn); break;
    default: spdlog::set_level(spdlog::level::info); break;
  }

  cells.rehash(config->initial_population);

  for (uint32_t i = 0; i < config->initial_population; ++i) {
    cells.insert({i, Cell(i)});
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
      // case SimulationType::DETERMINISTIC_RK4:
      //     deterministicStep();
      //     break;
  }
}

ecs::Run SimulationEngine::run(uint32_t steps) {
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
                << std::setprecision(1) << estimated_remaining_time << "s left " << cells.size()
                << " cells" << std::flush;

      spinner_index = (spinner_index + 1) % 4;
      last_update_time = current_time;
    }
  }
  std::cout << "\r\033[1;32mProgress: [";
  for (int j = 0; j < bar_width; ++j) {
    std::cout << "#";
  }
  std::cout << "] 100% \033[0m" << std::endl;

  return ecs::Run(std::move(cells),
                  std::move(available_mutation_types),
                  std::move(cells_graveyard),
                  std::move(generational_stat_report),
                  std::move(generational_popul_report),
                  total_deaths,
                  tau);
}

void SimulationEngine::stop() { spdlog::info("Simulation stopped"); }

void SimulationEngine::stochasticStep() {
  CELLEVOX_PROFILE_PHASE("stochastic_step_total");
  tau += config->tau_step;
  CellEvoX::systems::applyCommonPopulationStep(cells,
                                               cells_graveyard,
                                               *config,
                                               available_mutation_types,
                                               total_mutation_probability,
                                               actual_population,
                                               total_deaths,
                                               tau,
                                               rng);

  int current_tau = static_cast<int>(tau);
  if (current_tau % config->stat_res == 0 && current_tau != last_stat_snapshot_tau) {
    CELLEVOX_PROFILE_PHASE("stat_snapshot");
    takeStatSnapshot();
    last_stat_snapshot_tau = current_tau;
  }
  if (current_tau % config->popul_res == 0 && current_tau != last_population_snapshot_tau) {
    CELLEVOX_PROFILE_PHASE("population_snapshot");
    takePopulationSnapshot();
    last_population_snapshot_tau = current_tau;
  }

  if (config->graveyard_pruning_interval > 0 && 
      current_tau % config->graveyard_pruning_interval == 0 && 
      current_tau != last_pruning_tau) {
      CELLEVOX_PROFILE_PHASE("graveyard_pruning");
      pruneGraveyard();
      last_pruning_tau = current_tau;
  }
  
  // Log memory usage periodically (e.g. same as stats resolution or separate)
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

  size_t living_cells_count = cells.size();

  std::vector<uint32_t> sorted_keys;
  sorted_keys.reserve(living_cells_count);
  for (const auto& cell : cells) {
    sorted_keys.push_back(cell.first);
  }
  std::sort(sorted_keys.begin(), sorted_keys.end());

  for (uint32_t key : sorted_keys) {
    CellMap::const_accessor accessor;
    if (!cells.find(accessor, key)) continue;
    const auto& cell_val = accessor->second;

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

  // Compute means
  double mean_fitness = total_fitness / living_cells_count;
  double mean_mutations = total_mutations / living_cells_count;

  // Compute raw moments
  double M2_fitness = total_fitness_squared / living_cells_count;
  double M3_fitness = total_fitness_cubed / living_cells_count;
  double M4_fitness = total_fitness_fourth / living_cells_count;

  double M2_mutations = total_mutations_squared / living_cells_count;
  double M3_mutations = total_mutations_cubed / living_cells_count;
  double M4_mutations = total_mutations_fourth / living_cells_count;

  // Compute variances
  double fitness_variance = M2_fitness - mean_fitness * mean_fitness;
  double mutations_variance = M2_mutations - mean_mutations * mean_mutations;

  // Compute central moments (skewness and kurtosis components)
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

  // Store the snapshot
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
  snapshot_records.reserve(cells.size());
  const auto payload_kind = config->full_mutation_payload
                                ? CellEvoX::io::MutationPayloadKind::Full
                                : CellEvoX::io::MutationPayloadKind::DriverOnly;

  CellMap cells_copy;
  cells_copy.rehash(cells.size());
  for (const auto& cell : cells) {
    CellMap::accessor accessor;
    cells_copy.insert(accessor, {cell.first, cell.second});

    const uint32_t mutation_payload_offset = static_cast<uint32_t>(mutation_payload.size());
    for (const auto& [mutation_id, mutation_type] : cell.second.mutations) {
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
        {cell.first,
         cell.second.parent_id,
         cell.second.fitness,
         std::numeric_limits<float>::quiet_NaN(),
         std::numeric_limits<float>::quiet_NaN(),
         std::numeric_limits<float>::quiet_NaN(),
         static_cast<uint16_t>(
             std::min<size_t>(cell.second.mutations.size(), std::numeric_limits<uint16_t>::max())),
         mutation_payload_count,
         mutation_payload_offset,
         0,
         {0, 0, 0}});
  }

  const auto snapshot_path =
      CellEvoX::io::populationSnapshotPath(config->output_path, static_cast<int>(tau));
  if (!CellEvoX::io::writePopulationSnapshot(
          snapshot_path, tau, 0, snapshot_records, mutation_payload, payload_kind)) {
    spdlog::error("Failed to write population snapshot file: {}", snapshot_path);
  }

  generational_popul_report.push_back({tau, std::move(cells_copy)});
}

void SimulationEngine::pruneGraveyard() {
  spdlog::info("Pruning graveyard... Current size: {}", cells_graveyard.size());
  
  // 1. Identify all living cells (potential starting points)
  std::unordered_set<uint32_t> living_ids;
  for (const auto& cell : cells) {
      living_ids.insert(cell.first);
  }

  // 2. Traverse up the lineage to mark all ancestors
  std::unordered_set<uint32_t> reachable_dead_cells;
  
  // Use a stack for non-recursive traversal
  // We need to check both living cells' parents and already reachable dead cells' parents
  // But strictly, we only care about ancestors of currently living cells.
  
  for (uint32_t start_id : living_ids) {
      uint32_t current_id = start_id;
      
      // Get parent of current cell
      // Since it's living, we look in 'cells' map first to get its parent (not stored directly in cell?)
      // Wait, Cell struct has parent_id?
      // Let's check Cell definition. Run.hpp includes ecs/Cell.hpp
      
      // Assuming Cell has parent_id. 
      // If the cell is alive, we get its parent.
      
      CellMap::const_accessor accessor;
      if (cells.find(accessor, current_id)) {
          uint32_t parent_id = accessor->second.parent_id;
          
          // Traverse up
          while (parent_id != 0) {
              // If we already visited this parent, we can stop this branch
              if (reachable_dead_cells.count(parent_id) || living_ids.count(parent_id)) {
                  break;
              }
              
              // Check if parent is in graveyard
              Graveyard::const_accessor grave_accessor;
              if (cells_graveyard.find(grave_accessor, parent_id)) {
                  reachable_dead_cells.insert(parent_id);
                  parent_id = grave_accessor->second.first; // Get grandparent
              } else if (cells.find(accessor, parent_id)) {
                  // Parent is alive, no need to add to unreachable dead (obviously)
                  // But we continue traversal from it? 
                  // If parent is alive, it's already in living_ids, so it will be processed in outer loop.
                  // So we can stop here.
                  break;
              } else {
                  // Parent not found in living or graveyard? (Maybe deleted root or error)
                  break;
              }
          }
      }
  }
  
  // 3. Remove unreachable dead cells
  // tbb::concurrent_hash_map doesn't support easy iteration-deletion.
  // We can collect IDs to remove or build a new map.
  // Given we expect to remove a lot, building a new map might be better?
  // Or just iterate and erase if not in set.
  
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

size_t SimulationEngine::getRSS() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
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
    size_t cells_count = cells.size();
    size_t graveyard_count = cells_graveyard.size();
    
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
