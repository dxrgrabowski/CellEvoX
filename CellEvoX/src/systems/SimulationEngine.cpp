#include "systems/SimulationEngine.hpp"

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_pipeline.h>
#include <tbb/tbb.h>

#include <Eigen/Dense>
#include <chrono>
#include <cmath>
#include <execution>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <unordered_set>

#include "utils/MathUtils.hpp"
#include <unistd.h>
#include "utils/SimulationConfig.hpp"

using namespace utils;

std::atomic<bool> SimulationEngine::shutdown_requested{false};

void SimulationEngine::signalHandler(int signum) {
  spdlog::warn("\nReceived interrupt signal ({}). Gracefully shutting down...", signum);
  shutdown_requested.store(true);
}

SimulationEngine::SimulationEngine(std::shared_ptr<SimulationConfig> config)
    : tau(0.0), config(config), actual_population(config->initial_population), total_deaths(0) {
  
  // Set global spdlog level based on config verbosity
  switch (config->verbosity) {
    case 0: spdlog::set_level(spdlog::level::off); break;
    case 1: spdlog::set_level(spdlog::level::warn); break;
    default: spdlog::set_level(spdlog::level::info); break;
  }

  cells.rehash(config->initial_population);

  // Initialize cells with random 3D positions inside a sphere.
  // Sphere radius scales with cbrt(N) to avoid extreme initial density.
  const float init_radius = std::cbrt(static_cast<float>(config->initial_population)) *
                            Cell::CELL_RADIUS * 0.5f;
  std::mt19937 pos_gen(42);  // Deterministic seed for reproducibility
  std::uniform_real_distribution<float> uni(-1.0f, 1.0f);

  for (uint32_t i = 0; i < config->initial_population; ++i) {
    Cell c(i);
    // Rejection sampling for uniform distribution inside a sphere
    Eigen::Vector3f pos;
    do {
      pos = Eigen::Vector3f(uni(pos_gen), uni(pos_gen), uni(pos_gen));
    } while (pos.squaredNorm() > 1.0f);
    c.position = pos * init_radius;
    cells.insert({i, std::move(c)});
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

  spdlog::info("=== Simulation Engine Initialized (3D Spatial ABM) ===");
  spdlog::info("Initial population: {}, Capacity: {}", config->initial_population, config->env_capacity);
  spdlog::info("Tau step: {}, Total mutation probability: {:.6f}", config->tau_step, total_mutation_probability);
  spdlog::info("Init sphere radius: {:.2f}, Max local density: {:.1f}", init_radius, config->max_local_density);

  // Initialize memory logging
  std::string memory_log_path = config->output_path + "/statistics/memory_log.csv";
  memory_log_file.open(memory_log_path);
  if (memory_log_file.is_open()) {
    memory_log_file << "Tau,RSS_KB,Cells_Count,Graveyard_Count,Estimated_Cells_KB,Estimated_Graveyard_KB\n";
  } else {
    spdlog::warn("Failed to open memory log file at: {}", memory_log_path);
  }
}

void SimulationEngine::step() {
  switch (config->sim_type) {
    case SimulationType::STOCHASTIC_TAU_LEAP:
      stochasticStep();
      break;
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
                  total_deaths,
                  tau);
}

void SimulationEngine::stop() { spdlog::info("Simulation stopped"); }

/// Generate N exponentially-distributed random values (rate=1.0).
/// Thread-local RNG avoids contention.
static Eigen::VectorXd generateExponentialDistribution(int size) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::exponential_distribution<> exp_dist(1.0);

  Eigen::VectorXd result(size);
  for (int i = 0; i < size; ++i) {
    result(i) = exp_dist(gen);
  }
  return result;
}

// ============================================================================
// stochasticStep() — 3D Spatial ABM with local density
// ============================================================================
//
// Complexity breakdown:
//   1. Build active_cell_ids:         O(N) sequential iteration over CellMap
//   2. Extract positions:             O(N) parallel
//   3. Build SpatialHashGrid:         O(N log N) parallel_sort
//   4. Compute local density:         O(N) parallel, O(1) per-cell amortized
//   5. Generate random vectors:       O(N)
//   6. Birth/death parallel_for:      O(N) parallel
//   7. Insert newborns / erase dead:  O(births + deaths) sequential on CellMap
//   8. Mechanical relaxation:         O(mech_iterations * N log N)
//
// Total per tau-step: O(mech_iterations * N log N)
// ============================================================================
void SimulationEngine::stochasticStep() {
  double tau_step = config->tau_step;
  tau += tau_step;
  const size_t N = actual_population;

  // --- Step 1: Build contiguous active cell index vector ---
  // O(N) — iterate CellMap once for maximal cache locality later.
  active_cell_ids.clear();
  active_cell_ids.reserve(N);
  for (auto it = cells.begin(); it != cells.end(); ++it) {
    active_cell_ids.push_back(it->first);
  }

  if (active_cell_ids.size() != N) {
    spdlog::error("Mismatch in alive cell count: expected {}, found {}",
                  N, active_cell_ids.size());
  }

  // --- Step 2: Extract positions into contiguous SoA buffer ---
  // O(N) parallel — enables SIMD-friendly grid build and density queries.
  positions_read.resize(N);
  tbb::parallel_for(
      tbb::blocked_range<size_t>(0, N),
      [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i != range.end(); ++i) {
          CellMap::const_accessor acc;
          if (cells.find(acc, active_cell_ids[i])) {
            positions_read[i] = acc->second.position;
          }
        }
      });

  // --- Step 3: Build spatial hash grid from current positions ---
  // O(N log N) — parallel_sort dominates.
  grid.rebuild(positions_read.data(), static_cast<uint32_t>(N));

  // --- Step 4: Compute local density per cell ---
  // O(N) parallel, O(1) per-cell amortized via grid neighbor query.
  std::vector<float> local_density(N);
  const float sample_radius = config->sample_radius;
  tbb::parallel_for(
      tbb::blocked_range<size_t>(0, N),
      [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i != range.end(); ++i) {
          local_density[i] = static_cast<float>(
              grid.countNeighborsInRadius(positions_read[i], sample_radius,
                                          positions_read.data(),
                                          static_cast<uint32_t>(i)));
        }
      });

  // --- Step 5: Generate stochastic probability vectors ---
  Eigen::VectorXd exp_dist_death = generateExponentialDistribution(N);
  Eigen::VectorXd exp_dist_birth = generateExponentialDistribution(N);

  // Build fitness vector for birth probability denominator.
  Eigen::VectorXd fitness_vec =
      FitnessCalculator::getCellsFitnessVector(cells, active_cell_ids);

  // --- Step 6: Birth/death decisions with local density ---
  // O(N) parallel.  Each cell independently decides birth/death based on
  // local density ρ_i rather than global N/Nc.
  //
  // Death:  death_prob[i] = exp[i] / s_i   where s_i = max(1, ρ_i / ρ_max)
  //         → crowding increases mortality.
  // Birth:  birth_prob[i] = exp[i] / (fitness[i] * max(0.01, 1 - ρ_i/ρ_max))
  //         → crowding inhibits division.
  const float max_local_density = config->max_local_density;
  const float spawn_offset = config->spawn_offset;

  tbb::concurrent_vector<Cell> new_cells;
  tbb::concurrent_vector<uint32_t> dead_cells;
  std::atomic<uint32_t> new_cells_count(0), death_count(0);

  tbb::parallel_for(
      tbb::blocked_range<size_t>(0, N),
      [&](const tbb::blocked_range<size_t>& range) {
        // Thread-local RNG avoids contention on random number generation.
        thread_local std::mt19937 tl_gen(std::random_device{}());
        thread_local std::uniform_real_distribution<float> tl_uni(-1.0f, 1.0f);

        for (size_t i = range.begin(); i != range.end(); ++i) {
          uint32_t idx = active_cell_ids[i];
          CellMap::accessor cell;
          if (!cells.find(cell, idx)) continue;

          float rho_i = local_density[i];
          float s_i = std::max(1.0f, rho_i / max_local_density);

          // Local death probability: crowding increases death rate.
          double death_prob = exp_dist_death[i] / static_cast<double>(s_i);

          // Local birth probability: crowding inhibits division.
          float growth_room = std::max(0.01f, 1.0f - rho_i / max_local_density);
          double birth_prob = exp_dist_birth[i] /
                              (fitness_vec[i] * static_cast<double>(growth_room));

          if (death_prob <= tau_step) {
            // --- Cell death ---
            cells_graveyard.insert({cell->first, {cell->second.parent_id, tau}});
            dead_cells.push_back(idx);
            death_count++;
          } else if (birth_prob <= tau_step) {
            // --- Cell division ---
            new_cells_count += 2;

            // Parent dies (replaced by two daughters)
            cells_graveyard.insert({cell->first, {cell->second.parent_id, tau}});
            dead_cells.push_back(idx);
            death_count++;

            // Place daughters near parent with small random offset
            Eigen::Vector3f parent_pos = cell->second.position;
            auto makeOffset = [&]() -> Eigen::Vector3f {
              Eigen::Vector3f dir;
              do {
                dir = Eigen::Vector3f(tl_uni(tl_gen), tl_uni(tl_gen), tl_uni(tl_gen));
              } while (dir.squaredNorm() < 1e-6f);
              return dir.normalized() * spawn_offset;
            };

            // Mutation check
            double rand_val = (Eigen::VectorXd::Random(1)(0) + 1.0) / 2.0;
            if (rand_val >= total_mutation_probability) {
              Cell d1(cell->second, cell->second.fitness);
              d1.position = parent_pos + makeOffset();
              Cell d2(cell->second, cell->second.fitness);
              d2.position = parent_pos + makeOffset();
              new_cells.push_back(std::move(d1));
              new_cells.push_back(std::move(d2));
            } else {
              double prob_sum = 0.0;
              for (const auto& mut : available_mutation_types) {
                prob_sum += mut.second.probability;
                if (rand_val < prob_sum) {
                  Cell d1(cell->second,
                          cell->second.fitness * (1.0 + mut.second.effect));
                  d1.mutations.push_back({0, mut.second.type_id});
                  d1.position = parent_pos + makeOffset();
                  Cell d2(cell->second, cell->second.fitness);
                  d2.position = parent_pos + makeOffset();
                  new_cells.push_back(std::move(d1));
                  new_cells.push_back(std::move(d2));
                  break;
                }
              }
            }
          }
        }
      });

  // --- Step 7: Sequential insert / erase on CellMap ---
  auto starting_id = N + total_deaths;
  for (size_t i = 0; i < new_cells.size(); ++i) {
    CellMap::accessor accessor;
    new_cells[i].id = starting_id + i;
    if (!cells.insert(accessor, {starting_id + i, std::move(new_cells[i])})) {
      spdlog::error("Failed to insert new cell {}", starting_id + i);
    }
    for (auto& mut : accessor->second.mutations) {
      if (mut.first == 0) mut.first = starting_id + i;
    }
  }
  for (const auto& dead_id : dead_cells) {
    cells.erase(dead_id);
  }
  total_deaths += death_count;
  actual_population = actual_population + new_cells_count - death_count;

  // --- Step 8: Mechanical relaxation (force-based pushing) ---
  // Resolves overlaps created by daughter cell placement.
  if (actual_population > 0) {
    mechanicalRelaxationStep();
  }

  // --- Periodic snapshots & logging ---
  int current_tau = static_cast<int>(tau);
  if (current_tau % config->stat_res == 0 && current_tau != last_stat_snapshot_tau) {
    takeStatSnapshot();
    last_stat_snapshot_tau = current_tau;
  }
  if (current_tau % config->popul_res == 0 && current_tau != last_population_snapshot_tau) {
    takePopulationSnapshot();
    last_population_snapshot_tau = current_tau;
  }

  if (config->graveyard_pruning_interval > 0 &&
      current_tau % config->graveyard_pruning_interval == 0 &&
      current_tau != last_pruning_tau) {
    pruneGraveyard();
    last_pruning_tau = current_tau;
  }

  if (current_tau % config->stat_res == 0) {
    logMemoryUsage();
  }
}

// ============================================================================
// mechanicalRelaxationStep() — Lock-free double-buffered pushing
// ============================================================================
//
// Resolves physical overlaps via Hooke's law repulsion.
// Uses double-buffering: reads from positions_read, writes to positions_write.
// Each index is written by exactly one thread → zero data races, no mutexes.
//
// Per sub-step complexity: O(N log N) for grid rebuild, O(N) for force calc.
// Total: O(mech_iterations * N log N).
// ============================================================================
void SimulationEngine::mechanicalRelaxationStep() {
  const int iterations = config->mech_iterations;
  const float dt = config->mech_dt;
  constexpr float k = 1.0f;              // Hooke constant
  constexpr float two_R = 2.0f * Cell::CELL_RADIUS;
  constexpr float min_dist = 1e-4f;      // Avoid division by zero

  // Rebuild active cell list (population may have changed after birth/death)
  active_cell_ids.clear();
  active_cell_ids.reserve(actual_population);
  for (auto it = cells.begin(); it != cells.end(); ++it) {
    active_cell_ids.push_back(it->first);
  }
  const uint32_t N = static_cast<uint32_t>(active_cell_ids.size());

  // Extract current positions into contiguous buffer
  positions_read.resize(N);
  tbb::parallel_for(
      tbb::blocked_range<uint32_t>(0, N),
      [&](const tbb::blocked_range<uint32_t>& range) {
        for (uint32_t i = range.begin(); i != range.end(); ++i) {
          CellMap::const_accessor acc;
          if (cells.find(acc, active_cell_ids[i])) {
            positions_read[i] = acc->second.position;
          }
        }
      });

  positions_write.resize(N);

  for (int iter = 0; iter < iterations; ++iter) {
    // Rebuild grid from current positions for this sub-step. O(N log N).
    grid.rebuild(positions_read.data(), N);

    // Compute forces and new positions in parallel.  O(N), O(1) per cell.
    // Read from positions_read (shared, read-only)
    // Write to positions_write[i] (each i owned by exactly one thread)
    tbb::parallel_for(
        tbb::blocked_range<uint32_t>(0, N),
        [&](const tbb::blocked_range<uint32_t>& range) {
          for (uint32_t i = range.begin(); i != range.end(); ++i) {
            Eigen::Vector3f force = Eigen::Vector3f::Zero();
            const Eigen::Vector3f& xi = positions_read[i];

            // Iterate neighbors in the 27 surrounding voxels
            grid.forEachNeighbor(xi, [&](uint32_t j) {
              if (j == i) return;  // Skip self
              const Eigen::Vector3f& xj = positions_read[j];
              Eigen::Vector3f diff = xi - xj;
              float dist = diff.norm();
              if (dist < two_R && dist > min_dist) {
                // Hooke's law repulsion: F = k * (2R - d) * (xi - xj) / d
                float overlap = two_R - dist;
                force += (k * overlap / dist) * diff;
              }
            });

            // Euler integration: Δx = F * dt
            positions_write[i] = xi + force * dt;
          }
        });

    // Swap buffers: new positions become the read source for next iteration.
    std::swap(positions_read, positions_write);
  }

  // Write final positions back to CellMap.  O(N) parallel.
  tbb::parallel_for(
      tbb::blocked_range<uint32_t>(0, N),
      [&](const tbb::blocked_range<uint32_t>& range) {
        for (uint32_t i = range.begin(); i != range.end(); ++i) {
          CellMap::accessor acc;
          if (cells.find(acc, active_cell_ids[i])) {
            acc->second.position = positions_read[i];
          }
        }
      });
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

  // Guard against zero population (all cells died this step)
  if (living_cells_count == 0) {
    generational_stat_report.push_back({tau, 0.0, 0.0, 0.0, 0.0, 0, 0.0, 0.0, 0.0, 0.0});
    return;
  }

  for (const auto& cell : cells) {
    double f = cell.second.fitness;
    double f2 = f * f;
    double f3 = f2 * f;
    double f4 = f3 * f;

    double m = static_cast<double>(cell.second.mutations.size());
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

// ============================================================================
// takePopulationSnapshot() — Binary snapshot written directly to disk
// ============================================================================
//
// Replaces the old deep-copy-CellMap-to-vector approach.
// Uses packed CellSnapshotBinary (25 bytes/cell) and raw ostream::write
// for minimal I/O overhead.  10^6 cells → ~25 MB per snapshot.
// ============================================================================
void SimulationEngine::takePopulationSnapshot() {
  std::string path =
      config->output_path + "/population_data/population_tau_" +
      std::to_string(static_cast<int>(tau)) + ".bin";
  writeBinarySnapshot(path);
}

void SimulationEngine::writeBinarySnapshot(const std::string& path) {
  // Ensure directory exists
  auto dir = std::filesystem::path(path).parent_path();
  if (!dir.empty() && !std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }

  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    spdlog::warn("Failed to open binary snapshot file: {}", path);
    return;
  }

  // Write header: cell count (uint32_t)
  uint32_t count = static_cast<uint32_t>(cells.size());
  out.write(reinterpret_cast<const char*>(&count), sizeof(count));

  // Build flat snapshot buffer.  O(N) sequential.
  std::vector<CellSnapshotBinary> buffer;
  buffer.reserve(count);
  for (const auto& [cell_id, cell] : cells) {
    CellSnapshotBinary snap;
    snap.id = cell.id;
    snap.parent_id = cell.parent_id;
    snap.fitness = cell.fitness;
    snap.x = cell.position.x();
    snap.y = cell.position.y();
    snap.z = cell.position.z();
    snap.mutations_count = static_cast<uint8_t>(
        std::min(static_cast<size_t>(255), cell.mutations.size()));
    buffer.push_back(snap);
  }

  // Single bulk write — much faster than per-cell I/O.
  out.write(reinterpret_cast<const char*>(buffer.data()),
            buffer.size() * sizeof(CellSnapshotBinary));
}

void SimulationEngine::pruneGraveyard() {
  spdlog::info("Pruning graveyard... Current size: {}", cells_graveyard.size());
  
  std::unordered_set<uint32_t> living_ids;
  for (const auto& cell : cells) {
    living_ids.insert(cell.first);
  }

  std::unordered_set<uint32_t> reachable_dead_cells;
  
  for (uint32_t start_id : living_ids) {
    uint32_t current_id = start_id;
    CellMap::const_accessor accessor;
    if (cells.find(accessor, current_id)) {
      uint32_t parent_id = accessor->second.parent_id;
      
      while (parent_id != 0) {
        if (reachable_dead_cells.count(parent_id) || living_ids.count(parent_id)) {
          break;
        }
        
        Graveyard::const_accessor grave_accessor;
        if (cells_graveyard.find(grave_accessor, parent_id)) {
          reachable_dead_cells.insert(parent_id);
          parent_id = grave_accessor->second.first;
        } else if (cells.find(accessor, parent_id)) {
          break;
        } else {
          break;
        }
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

size_t SimulationEngine::getRSS() {
  size_t rss = 0;
  std::ifstream statm("/proc/self/statm");
  if (statm.is_open()) {
    size_t ignore;
    statm >> ignore >> rss;
  }
  long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
  return rss * page_size_kb;
}

void SimulationEngine::logMemoryUsage() {
  if (!memory_log_file.is_open()) return;

  size_t rss_kb = getRSS();
  size_t cells_count = cells.size();
  size_t graveyard_count = cells_graveyard.size();
  
  size_t estimated_cells_kb = (cells_count * sizeof(Cell)) / 1024;
  size_t estimated_graveyard_kb = (graveyard_count * 48) / 1024;

  memory_log_file << tau << ","
                  << rss_kb << ","
                  << cells_count << ","
                  << graveyard_count << ","
                  << estimated_cells_kb << ","
                  << estimated_graveyard_kb << "\n";
  memory_log_file.flush();
}