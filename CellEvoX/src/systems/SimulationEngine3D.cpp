#include "systems/SimulationEngine3D.hpp"

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/combinable.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_set>

#include "io/PopulationSnapshotIO.hpp"
#include <unistd.h>

namespace {

constexpr uint32_t kInvalidSpatialIndex = std::numeric_limits<uint32_t>::max();
constexpr float kBirthSuppressionFloor = 0.01f;
constexpr float kDeathRateFloor = 0.01f;
constexpr float kCrowdingPenaltySplit = 0.5f;

}  // namespace

std::atomic<bool> SimulationEngine3D::shutdown_requested{false};

void SimulationEngine3D::signalHandler(int signum) {
  spdlog::warn("\nReceived interrupt signal ({}). Gracefully shutting down...", signum);
  shutdown_requested.store(true);
}

SimulationEngine3D::SimulationEngine3D(std::shared_ptr<SimulationConfig> config)
    : actual_population(config->initial_population),
      total_deaths(0),
      tau(0.0),
      total_mutation_probability(0.0),
      next_cell_id_(static_cast<uint32_t>(config->initial_population)),
      config(std::move(config)),
      rng(this->config->seed),
      spatial_grid_(2.0f * CELL_RADIUS, this->config->spatial_domain_size) {
  switch (this->config->verbosity) {
    case 0:
      spdlog::set_level(spdlog::level::off);
      break;
    case 1:
      spdlog::set_level(spdlog::level::warn);
      break;
    default:
      spdlog::set_level(spdlog::level::info);
      break;
  }

  std::filesystem::create_directories(
      std::filesystem::path(this->config->output_path) / "statistics");
  std::filesystem::create_directories(
      std::filesystem::path(this->config->output_path) / "population_data");

  cells.rehash(this->config->initial_population * 2 + 1);
  for (uint32_t id = 0; id < this->config->initial_population; ++id) {
    cells.insert({id, Cell(id)});
  }

  for (const auto& mutation : this->config->mutations) {
    available_mutation_types[mutation.type_id] = mutation;
  }

  total_mutation_probability =
      std::accumulate(available_mutation_types.begin(),
                      available_mutation_types.end(),
                      0.0,
                      [](double sum, const std::pair<const uint8_t, MutationType>& mutation) {
                        return sum + mutation.second.probability;
                      });

  initializePopulationPositions();
  rebuildSpatialState();

  const std::string memory_log_path = this->config->output_path + "/statistics/memory_log.csv";
  memory_log_file.open(memory_log_path);
  if (memory_log_file.is_open()) {
    memory_log_file << "Tau,RSS_KB,Cells_Count,Graveyard_Count,Estimated_Cells_KB,"
                       "Estimated_Graveyard_KB\n";
  }

  spdlog::info("=== Spatial 3D Simulation Engine Initialized ===");
  spdlog::info("Initial population: {}, Domain: {:.2f}, Tau step: {:.3f}",
               this->config->initial_population,
               this->config->spatial_domain_size,
               this->config->tau_step);
}

ecs::Run SimulationEngine3D::run(uint32_t steps) {
  auto last_update_time = std::chrono::steady_clock::now();
  const char* spinner = "|/-\\";
  int spinner_index = 0;
  const int bar_width = 50;

  const auto start_time = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < steps; ++i) {
    if (shutdown_requested.load()) {
      spdlog::info("Shutdown requested at step {}/{}", i, steps);
      std::cout << std::endl;
      break;
    }

    step();

    const auto current_time = std::chrono::steady_clock::now();
    const auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  current_time - last_update_time)
                                  .count();

    if (elapsed_time >= 100) {
      const int progress = static_cast<int>((static_cast<double>(i + 1) / steps) * 100.0);
      const int pos = static_cast<int>((static_cast<double>(i + 1) / steps) * bar_width);
      const auto total_elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
      const double avg_time_per_step = static_cast<double>(total_elapsed) / (i + 1);
      const int remaining_steps = steps - (i + 1);
      const double estimated_remaining_time = remaining_steps * avg_time_per_step / 1000.0;

      std::cout << "\r\033[1;32mProgress: [\033[35m";
      for (int j = 0; j < bar_width; ++j) {
        std::cout << (j < pos ? '#' : ' ');
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

void SimulationEngine3D::step() {
  if (config->sim_type == SimulationType::SPATIAL_3D_ABM) {
    stochasticStep3D();
  }
}

void SimulationEngine3D::stop() { spdlog::info("Spatial 3D simulation stopped"); }

void SimulationEngine3D::initializePopulationPositions() {
  id_pos_x_.assign(next_cell_id_, 0.0f);
  id_pos_y_.assign(next_cell_id_, 0.0f);
  id_pos_z_.assign(next_cell_id_, 0.0f);
  id_to_spatial_index_.assign(next_cell_id_, kInvalidSpatialIndex);

  const uint32_t initial_population = static_cast<uint32_t>(config->initial_population);
  if (initial_population == 0) {
    return;
  }

  const uint32_t cells_per_axis =
      std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::cbrt(initial_population))));
  const float spacing = config->spatial_domain_size / static_cast<float>(cells_per_axis);
  std::uniform_real_distribution<float> jitter_dist(-0.1f * CELL_RADIUS, 0.1f * CELL_RADIUS);

  // O(N) lattice initialization keeps the starting state spatially stable.
  for (uint32_t id = 0; id < initial_population; ++id) {
    const uint32_t ix = id % cells_per_axis;
    const uint32_t iy = (id / cells_per_axis) % cells_per_axis;
    const uint32_t iz = id / (cells_per_axis * cells_per_axis);

    id_pos_x_[id] =
        clampToDomain((static_cast<float>(ix) + 0.5f) * spacing + jitter_dist(rng));
    id_pos_y_[id] =
        clampToDomain((static_cast<float>(iy) + 0.5f) * spacing + jitter_dist(rng));
    id_pos_z_[id] =
        clampToDomain((static_cast<float>(iz) + 0.5f) * spacing + jitter_dist(rng));
  }
}

void SimulationEngine3D::rebuildSpatialState() {
  for (uint32_t id : spatial_state_.cell_ids) {
    if (id < id_to_spatial_index_.size()) {
      id_to_spatial_index_[id] = kInvalidSpatialIndex;
    }
  }

  spatial_state_.cell_ids.clear();
  spatial_state_.pos_x.clear();
  spatial_state_.pos_y.clear();
  spatial_state_.pos_z.clear();

  spatial_state_.cell_ids.reserve(cells.size());
  for (const auto& cell_entry : cells) {
    spatial_state_.cell_ids.push_back(cell_entry.first);
  }
  std::sort(spatial_state_.cell_ids.begin(), spatial_state_.cell_ids.end());

  spatial_state_.pos_x.reserve(spatial_state_.cell_ids.size());
  spatial_state_.pos_y.reserve(spatial_state_.cell_ids.size());
  spatial_state_.pos_z.reserve(spatial_state_.cell_ids.size());

  // O(N) gather from the persistent id-indexed position store.
  for (size_t i = 0; i < spatial_state_.cell_ids.size(); ++i) {
    const uint32_t id = spatial_state_.cell_ids[i];
    ensurePositionCapacity(id);
    spatial_state_.pos_x.push_back(id_pos_x_[id]);
    spatial_state_.pos_y.push_back(id_pos_y_[id]);
    spatial_state_.pos_z.push_back(id_pos_z_[id]);
    id_to_spatial_index_[id] = static_cast<uint32_t>(i);
  }

  spatial_grid_.rebuild(
      spatial_state_.cell_ids, spatial_state_.pos_x, spatial_state_.pos_y, spatial_state_.pos_z);
}

void SimulationEngine3D::stochasticStep3D() {
  const float tau_step = static_cast<float>(config->tau_step);
  tau += tau_step;

  if (spatial_state_.cell_ids.empty()) {
    return;
  }

  const float sample_radius = std::max(config->sample_radius, 0.1f);
  const float sample_radius_sq = sample_radius * sample_radius;
  const float max_local_density = std::max(config->max_local_density, 1.0f);

  tbb::combinable<std::vector<PendingBirth>> births_per_thread;
  tbb::combinable<std::vector<PendingDeath>> deaths_per_thread;

  // O(N) over active cells; each density query inspects a fixed voxel neighborhood.
  tbb::parallel_for(
      tbb::blocked_range<size_t>(0, spatial_state_.cell_ids.size()),
      [&](const tbb::blocked_range<size_t>& range) {
        auto& local_births = births_per_thread.local();
        auto& local_deaths = deaths_per_thread.local();
        std::exponential_distribution<float> exp_dist(1.0f);
        std::uniform_real_distribution<float> unif_dist(0.0f, 1.0f);
        std::mt19937& local_rng = getThreadLocalRng();

        for (size_t i = range.begin(); i != range.end(); ++i) {
          const uint32_t id = spatial_state_.cell_ids[i];
          const float x = spatial_state_.pos_x[i];
          const float y = spatial_state_.pos_y[i];
          const float z = spatial_state_.pos_z[i];

          uint32_t local_density = 0;
          spatial_grid_.queryRadius(x, y, z, sample_radius, [&](uint32_t neighbor_id) {
            if (neighbor_id == id) {
              return;
            }

            const uint32_t neighbor_index = id_to_spatial_index_[neighbor_id];
            if (neighbor_index == kInvalidSpatialIndex) {
              return;
            }

            const float dx = spatial_state_.pos_x[neighbor_index] - x;
            const float dy = spatial_state_.pos_y[neighbor_index] - y;
            const float dz = spatial_state_.pos_z[neighbor_index] - z;
            const float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq <= sample_radius_sq) {
              ++local_density;
            }
          });

          const float crowding_ratio =
              static_cast<float>(local_density) / max_local_density;

          CellMap::const_accessor cell_accessor;
          if (!cells.find(cell_accessor, id)) {
            continue;
          }
          const Cell parent = cell_accessor->second;

          // Split the crowding penalty between death and proliferation so the
          // neutral population stays approximately balanced near local capacity.
          const float death_rate =
              std::max(kDeathRateFloor, kCrowdingPenaltySplit * crowding_ratio);
          const float birth_rate =
              std::max(kBirthSuppressionFloor,
                       static_cast<float>(parent.fitness) *
                           (1.0f - kCrowdingPenaltySplit * crowding_ratio));

          const float death_prob = exp_dist(local_rng) / death_rate;
          const float birth_prob = exp_dist(local_rng) / birth_rate;

          if (death_prob < tau_step) {
            local_deaths.push_back({id, parent.parent_id});
            continue;
          }

          if (birth_prob >= tau_step) {
            continue;
          }

          local_deaths.push_back({id, parent.parent_id});

          // Symmetric placement avoids a persistent center-of-mass drift at division.
          const Eigen::Vector3f offset =
              sampleRandomUnitVector(local_rng) * (config->epsilon * CELL_RADIUS * 0.5f);

          PendingBirth first_daughter{Cell(parent, parent.fitness),
                                      clampToDomain(x - offset.x()),
                                      clampToDomain(y - offset.y()),
                                      clampToDomain(z - offset.z())};
          PendingBirth second_daughter{Cell(parent, parent.fitness),
                                       clampToDomain(x + offset.x()),
                                       clampToDomain(y + offset.y()),
                                       clampToDomain(z + offset.z())};

          const float mutation_roll = unif_dist(local_rng);
          if (mutation_roll < static_cast<float>(total_mutation_probability)) {
            float cumulative_probability = 0.0f;
            for (const auto& [mutation_id, mutation] : available_mutation_types) {
              cumulative_probability += mutation.probability;
              if (mutation_roll < cumulative_probability) {
                first_daughter.cell =
                    Cell(parent, parent.fitness * static_cast<double>(1.0f + mutation.effect));
                first_daughter.cell.mutations.push_back({0, mutation_id});
                break;
              }
            }
          }

          local_births.push_back(std::move(first_daughter));
          local_births.push_back(std::move(second_daughter));
        }
      });

  std::vector<PendingDeath> pending_deaths;
  deaths_per_thread.combine_each(
      [&](const std::vector<PendingDeath>& local_deaths) {
        pending_deaths.insert(pending_deaths.end(), local_deaths.begin(), local_deaths.end());
      });

  std::vector<PendingBirth> pending_births;
  births_per_thread.combine_each(
      [&](std::vector<PendingBirth>& local_births) {
        pending_births.insert(pending_births.end(),
                              std::make_move_iterator(local_births.begin()),
                              std::make_move_iterator(local_births.end()));
      });

  std::sort(
      pending_deaths.begin(),
      pending_deaths.end(),
      [](const PendingDeath& lhs, const PendingDeath& rhs) { return lhs.id < rhs.id; });

  std::sort(pending_births.begin(), pending_births.end(), [](const PendingBirth& lhs,
                                                             const PendingBirth& rhs) {
    if (lhs.cell.parent_id != rhs.cell.parent_id) {
      return lhs.cell.parent_id < rhs.cell.parent_id;
    }
    if (lhs.cell.fitness != rhs.cell.fitness) {
      return lhs.cell.fitness < rhs.cell.fitness;
    }
    if (lhs.cell.mutations.size() != rhs.cell.mutations.size()) {
      return lhs.cell.mutations.size() < rhs.cell.mutations.size();
    }
    if (!lhs.cell.mutations.empty() && !rhs.cell.mutations.empty() &&
        lhs.cell.mutations.back() != rhs.cell.mutations.back()) {
      return lhs.cell.mutations.back() < rhs.cell.mutations.back();
    }
    if (lhs.x != rhs.x) {
      return lhs.x < rhs.x;
    }
    if (lhs.y != rhs.y) {
      return lhs.y < rhs.y;
    }
    return lhs.z < rhs.z;
  });

  for (const auto& death : pending_deaths) {
    cells_graveyard.insert({death.id, {death.parent_id, tau}});
    cells.erase(death.id);
  }

  for (auto& birth : pending_births) {
    const uint32_t new_id = next_cell_id_++;
    birth.cell.id = new_id;
    for (auto& mutation : birth.cell.mutations) {
      if (mutation.first == 0) {
        mutation.first = new_id;
      }
    }

    ensurePositionCapacity(new_id);
    id_pos_x_[new_id] = birth.x;
    id_pos_y_[new_id] = birth.y;
    id_pos_z_[new_id] = birth.z;

    CellMap::accessor accessor;
    if (!cells.insert(accessor, {new_id, std::move(birth.cell)})) {
      spdlog::error("Failed to insert spatial daughter cell {}", new_id);
    }
  }

  total_deaths += pending_deaths.size();
  actual_population = cells.size();

  rebuildSpatialState();
  mechanicalRelaxationStep();

  const int current_tau = static_cast<int>(tau);
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

  if (config->stat_res > 0 && current_tau % config->stat_res == 0) {
    logMemoryUsage();
  }
}

void SimulationEngine3D::mechanicalRelaxationStep() {
  const size_t count = spatial_state_.cell_ids.size();
  if (count == 0 || config->mech_substeps <= 0) {
    return;
  }

  const float interaction_radius = 2.0f * CELL_RADIUS;
  const float interaction_radius_sq = interaction_radius * interaction_radius;

  std::vector<float> read_x = spatial_state_.pos_x;
  std::vector<float> read_y = spatial_state_.pos_y;
  std::vector<float> read_z = spatial_state_.pos_z;
  std::vector<float> write_x = read_x;
  std::vector<float> write_y = read_y;
  std::vector<float> write_z = read_z;

  for (int substep = 0; substep < config->mech_substeps; ++substep) {
    spatial_grid_.rebuild(spatial_state_.cell_ids, read_x, read_y, read_z);

    // O(N) over active cells with constant-radius neighbor lookups.
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, count),
        [&](const tbb::blocked_range<size_t>& range) {
          for (size_t i = range.begin(); i != range.end(); ++i) {
            const float xi = read_x[i];
            const float yi = read_y[i];
            const float zi = read_z[i];

            Eigen::Vector3f force = Eigen::Vector3f::Zero();
            spatial_grid_.queryRadius(xi, yi, zi, interaction_radius, [&](uint32_t neighbor_id) {
              const uint32_t neighbor_index = id_to_spatial_index_[neighbor_id];
              if (neighbor_index == kInvalidSpatialIndex || neighbor_index == i) {
                return;
              }

              const float dx = xi - read_x[neighbor_index];
              const float dy = yi - read_y[neighbor_index];
              const float dz = zi - read_z[neighbor_index];
              const float dist_sq = dx * dx + dy * dy + dz * dz;
              if (dist_sq <= 1e-12f || dist_sq >= interaction_radius_sq) {
                return;
              }

              const float dist = std::sqrt(dist_sq);
              const float overlap = interaction_radius - dist;
              if (overlap <= 0.0f) {
                return;
              }

              const float scale = config->spring_constant * overlap / dist;
              force.x() += scale * dx;
              force.y() += scale * dy;
              force.z() += scale * dz;
            });

            write_x[i] = clampToDomain(xi + force.x() * config->mech_dt);
            write_y[i] = clampToDomain(yi + force.y() * config->mech_dt);
            write_z[i] = clampToDomain(zi + force.z() * config->mech_dt);
          }
        });

    read_x.swap(write_x);
    read_y.swap(write_y);
    read_z.swap(write_z);
  }

  spatial_state_.pos_x = std::move(read_x);
  spatial_state_.pos_y = std::move(read_y);
  spatial_state_.pos_z = std::move(read_z);

  tbb::parallel_for(
      tbb::blocked_range<size_t>(0, count),
      [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i != range.end(); ++i) {
          const uint32_t id = spatial_state_.cell_ids[i];
          id_pos_x_[id] = spatial_state_.pos_x[i];
          id_pos_y_[id] = spatial_state_.pos_y[i];
          id_pos_z_[id] = spatial_state_.pos_z[i];
        }
      });

  spatial_grid_.rebuild(
      spatial_state_.cell_ids, spatial_state_.pos_x, spatial_state_.pos_y, spatial_state_.pos_z);
}

void SimulationEngine3D::takeStatSnapshot() {
  const size_t living_cells_count = cells.size();
  if (living_cells_count == 0) {
    generational_stat_report.push_back(
        {tau, 0.0, 0.0, 0.0, 0.0, 0, 0.0, 0.0, 0.0, 0.0});
    return;
  }

  double total_fitness = 0.0;
  double total_fitness_squared = 0.0;
  double total_fitness_cubed = 0.0;
  double total_fitness_fourth = 0.0;

  double total_mutations = 0.0;
  double total_mutations_squared = 0.0;
  double total_mutations_cubed = 0.0;
  double total_mutations_fourth = 0.0;

  std::vector<uint32_t> sorted_keys;
  sorted_keys.reserve(living_cells_count);
  for (const auto& cell : cells) {
    sorted_keys.push_back(cell.first);
  }
  std::sort(sorted_keys.begin(), sorted_keys.end());

  for (uint32_t key : sorted_keys) {
    CellMap::const_accessor accessor;
    if (!cells.find(accessor, key)) {
      continue;
    }

    const auto& cell = accessor->second;
    const double fitness = cell.fitness;
    const double fitness_sq = fitness * fitness;
    const double fitness_cu = fitness_sq * fitness;
    const double fitness_qd = fitness_cu * fitness;

    const double mutations = static_cast<double>(cell.mutations.size());
    const double mutations_sq = mutations * mutations;
    const double mutations_cu = mutations_sq * mutations;
    const double mutations_qd = mutations_cu * mutations;

    total_fitness += fitness;
    total_fitness_squared += fitness_sq;
    total_fitness_cubed += fitness_cu;
    total_fitness_fourth += fitness_qd;

    total_mutations += mutations;
    total_mutations_squared += mutations_sq;
    total_mutations_cubed += mutations_cu;
    total_mutations_fourth += mutations_qd;
  }

  const double mean_fitness = total_fitness / living_cells_count;
  const double mean_mutations = total_mutations / living_cells_count;

  const double raw_fitness_second = total_fitness_squared / living_cells_count;
  const double raw_fitness_third = total_fitness_cubed / living_cells_count;
  const double raw_fitness_fourth = total_fitness_fourth / living_cells_count;

  const double raw_mutations_second = total_mutations_squared / living_cells_count;
  const double raw_mutations_third = total_mutations_cubed / living_cells_count;
  const double raw_mutations_fourth = total_mutations_fourth / living_cells_count;

  const double fitness_variance = raw_fitness_second - mean_fitness * mean_fitness;
  const double mutations_variance = raw_mutations_second - mean_mutations * mean_mutations;

  const double fitness_skewness =
      raw_fitness_third - 3.0 * mean_fitness * raw_fitness_second +
      2.0 * std::pow(mean_fitness, 3);
  const double fitness_kurtosis =
      raw_fitness_fourth - 4.0 * mean_fitness * raw_fitness_third +
      6.0 * mean_fitness * mean_fitness * raw_fitness_second -
      3.0 * std::pow(mean_fitness, 4);

  const double mutations_skewness =
      raw_mutations_third - 3.0 * mean_mutations * raw_mutations_second +
      2.0 * std::pow(mean_mutations, 3);
  const double mutations_kurtosis =
      raw_mutations_fourth - 4.0 * mean_mutations * raw_mutations_third +
      6.0 * mean_mutations * mean_mutations * raw_mutations_second -
      3.0 * std::pow(mean_mutations, 4);

  generational_stat_report.push_back({tau,
                                      mean_fitness,
                                      fitness_variance,
                                      mean_mutations,
                                      mutations_variance,
                                      living_cells_count,
                                      fitness_skewness,
                                      fitness_kurtosis,
                                      mutations_skewness,
                                      mutations_kurtosis});
}

void SimulationEngine3D::takePopulationSnapshot() {
  std::vector<CellEvoX::io::PopulationSnapshotRecord> snapshot;
  std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> driver_mutations;
  snapshot.reserve(spatial_state_.cell_ids.size());

  // O(N) linear snapshot over the active spatial ordering.
  for (size_t i = 0; i < spatial_state_.cell_ids.size(); ++i) {
    const uint32_t id = spatial_state_.cell_ids[i];
    CellMap::const_accessor accessor;
    if (!cells.find(accessor, id)) {
      continue;
    }

    const uint32_t driver_mutation_offset = static_cast<uint32_t>(driver_mutations.size());
    for (const auto& [mutation_id, mutation_type] : accessor->second.mutations) {
      const auto type_it = available_mutation_types.find(mutation_type);
      if (type_it != available_mutation_types.end() && type_it->second.is_driver) {
        driver_mutations.push_back({mutation_id, mutation_type});
      }
    }
    const auto driver_mutation_count =
        static_cast<uint16_t>(std::min<size_t>(driver_mutations.size() - driver_mutation_offset,
                                               std::numeric_limits<uint16_t>::max()));

    snapshot.push_back({id,
                        accessor->second.parent_id,
                        accessor->second.fitness,
                        spatial_state_.pos_x[i],
                        spatial_state_.pos_y[i],
                        spatial_state_.pos_z[i],
                        static_cast<uint16_t>(std::min<size_t>(
                            accessor->second.mutations.size(),
                            std::numeric_limits<uint16_t>::max())),
                        driver_mutation_count,
                        driver_mutation_offset,
                        1,
                        {0, 0, 0}});
  }

  const auto filename =
      CellEvoX::io::populationSnapshotPath(config->output_path, static_cast<int>(tau));
  if (!CellEvoX::io::writePopulationSnapshot(filename, tau, 3, snapshot, driver_mutations)) {
    spdlog::error("Failed to write population snapshot file: {}", filename);
  }
}

void SimulationEngine3D::pruneGraveyard() {
  std::unordered_set<uint32_t> living_ids;
  for (const auto& cell : cells) {
    living_ids.insert(cell.first);
  }

  std::unordered_set<uint32_t> reachable_dead_cells;
  for (uint32_t start_id : living_ids) {
    CellMap::const_accessor accessor;
    if (!cells.find(accessor, start_id)) {
      continue;
    }

    uint32_t parent_id = accessor->second.parent_id;
    while (parent_id != 0) {
      if (reachable_dead_cells.count(parent_id) || living_ids.count(parent_id)) {
        break;
      }

      Graveyard::const_accessor grave_accessor;
      if (cells_graveyard.find(grave_accessor, parent_id)) {
        reachable_dead_cells.insert(parent_id);
        parent_id = grave_accessor->second.first;
        continue;
      }

      break;
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
}

Eigen::Vector3f SimulationEngine3D::sampleRandomUnitVector(std::mt19937& rng) const {
  std::normal_distribution<float> normal_dist(0.0f, 1.0f);
  Eigen::Vector3f direction(normal_dist(rng), normal_dist(rng), normal_dist(rng));
  const float norm = direction.norm();
  if (norm <= 1e-6f) {
    return Eigen::Vector3f::UnitX();
  }
  return direction / norm;
}

float SimulationEngine3D::clampToDomain(float value) const {
  return std::clamp(value, 0.0f, config->spatial_domain_size);
}

void SimulationEngine3D::ensurePositionCapacity(uint32_t id) {
  if (id < id_pos_x_.size()) {
    return;
  }

  const size_t new_size = static_cast<size_t>(id) + 1;
  id_pos_x_.resize(new_size, 0.0f);
  id_pos_y_.resize(new_size, 0.0f);
  id_pos_z_.resize(new_size, 0.0f);
  id_to_spatial_index_.resize(new_size, kInvalidSpatialIndex);
}

std::mt19937& SimulationEngine3D::getThreadLocalRng() const {
  struct ThreadRngState {
    std::mt19937 rng;
    const SimulationEngine3D* owner = nullptr;
    int thread_index = std::numeric_limits<int>::min();
  };

  static thread_local ThreadRngState state;

  const int thread_index = tbb::this_task_arena::current_thread_index();
  if (state.owner != this || state.thread_index != thread_index) {
    const uint32_t seed = config->seed ^
                          (0x9E3779B9u + static_cast<uint32_t>(std::max(thread_index, 0)));
    state.rng.seed(seed);
    state.owner = this;
    state.thread_index = thread_index;
  }

  return state.rng;
}

size_t SimulationEngine3D::getRSS() {
  size_t rss = 0;
  std::ifstream statm("/proc/self/statm");
  if (statm.is_open()) {
    size_t ignored = 0;
    statm >> ignored >> rss;
  }

  const long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
  return rss * static_cast<size_t>(page_size_kb);
}

void SimulationEngine3D::logMemoryUsage() {
  if (!memory_log_file.is_open()) {
    return;
  }

  const size_t rss_kb = getRSS();
  const size_t cells_count = cells.size();
  const size_t graveyard_count = cells_graveyard.size();
  const size_t estimated_cells_kb = (cells_count * sizeof(Cell)) / 1024;
  const size_t estimated_graveyard_kb = (graveyard_count * 48) / 1024;

  memory_log_file << tau << "," << rss_kb << "," << cells_count << "," << graveyard_count << ","
                  << estimated_cells_kb << "," << estimated_graveyard_kb << "\n";
  memory_log_file.flush();
}
