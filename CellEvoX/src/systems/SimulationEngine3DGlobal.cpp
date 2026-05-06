#include "systems/SimulationEngine3DGlobal.hpp"

#include <spdlog/spdlog.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <unordered_set>

#include "io/PopulationSnapshotIO.hpp"
#include "systems/GlobalPopulationStep.hpp"
#include <unistd.h>

namespace {

constexpr uint32_t kInvalidSpatialIndex = std::numeric_limits<uint32_t>::max();

}  // namespace

std::atomic<bool> SimulationEngine3DGlobal::shutdown_requested{false};

void SimulationEngine3DGlobal::signalHandler(int signum) {
  spdlog::warn("\nReceived interrupt signal ({}). Gracefully shutting down...", signum);
  shutdown_requested.store(true);
}

SimulationEngine3DGlobal::SimulationEngine3DGlobal(std::shared_ptr<SimulationConfig> config)
    : actual_population(config->initial_population),
      total_deaths(0),
      tau(0.0),
      total_mutation_probability(0.0),
      config(std::move(config)),
      event_rng_(this->config->seed),
      spatial_rng_(this->config->seed ^ 0xA5A5A5A5u),
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

  spdlog::info("=== Spatial 3D Global Simulation Engine Initialized ===");
  spdlog::info("Initial population: {}, Capacity: {}, Domain: {:.2f}, Tau step: {:.3f}",
               this->config->initial_population,
               this->config->env_capacity,
               this->config->spatial_domain_size,
               this->config->tau_step);
}

ecs::Run SimulationEngine3DGlobal::run(uint32_t steps) {
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

void SimulationEngine3DGlobal::step() {
  tau += config->tau_step;
  const auto step_result = CellEvoX::systems::applyGlobalPopulationStep(cells,
                                                                        cells_graveyard,
                                                                        *config,
                                                                        available_mutation_types,
                                                                        total_mutation_probability,
                                                                        actual_population,
                                                                        total_deaths,
                                                                        tau,
                                                                        event_rng_);

  assignBirthPositions(step_result.births);
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

void SimulationEngine3DGlobal::stop() {
  spdlog::info("Spatial 3D global simulation stopped");
}

void SimulationEngine3DGlobal::initializePopulationPositions() {
  id_pos_x_.assign(config->initial_population, 0.0f);
  id_pos_y_.assign(config->initial_population, 0.0f);
  id_pos_z_.assign(config->initial_population, 0.0f);
  id_to_spatial_index_.assign(config->initial_population, kInvalidSpatialIndex);

  const uint32_t initial_population = static_cast<uint32_t>(config->initial_population);
  if (initial_population == 0) {
    return;
  }

  const uint32_t cells_per_axis =
      std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::cbrt(initial_population))));
  const float spacing = config->spatial_domain_size / static_cast<float>(cells_per_axis);
  std::uniform_real_distribution<float> jitter_dist(-0.1f * CELL_RADIUS, 0.1f * CELL_RADIUS);

  for (uint32_t id = 0; id < initial_population; ++id) {
    const uint32_t ix = id % cells_per_axis;
    const uint32_t iy = (id / cells_per_axis) % cells_per_axis;
    const uint32_t iz = id / (cells_per_axis * cells_per_axis);

    id_pos_x_[id] =
        clampToDomain((static_cast<float>(ix) + 0.5f) * spacing + jitter_dist(spatial_rng_));
    id_pos_y_[id] =
        clampToDomain((static_cast<float>(iy) + 0.5f) * spacing + jitter_dist(spatial_rng_));
    id_pos_z_[id] =
        clampToDomain((static_cast<float>(iz) + 0.5f) * spacing + jitter_dist(spatial_rng_));
  }
}

void SimulationEngine3DGlobal::rebuildSpatialState() {
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

void SimulationEngine3DGlobal::assignBirthPositions(
    const std::vector<CellEvoX::systems::GlobalBirthEvent>& births) {
  size_t index = 0;
  while (index < births.size()) {
    const uint32_t parent_id = births[index].parent_id;
    ensurePositionCapacity(parent_id);

    const float parent_x = id_pos_x_[parent_id];
    const float parent_y = id_pos_y_[parent_id];
    const float parent_z = id_pos_z_[parent_id];
    const Eigen::Vector3f offset =
        sampleRandomUnitVector(spatial_rng_) * (config->epsilon * CELL_RADIUS * 0.5f);

    const uint32_t first_id = births[index].id;
    ensurePositionCapacity(first_id);
    id_pos_x_[first_id] = clampToDomain(parent_x - offset.x());
    id_pos_y_[first_id] = clampToDomain(parent_y - offset.y());
    id_pos_z_[first_id] = clampToDomain(parent_z - offset.z());

    if (index + 1 < births.size() && births[index + 1].parent_id == parent_id) {
      const uint32_t second_id = births[index + 1].id;
      ensurePositionCapacity(second_id);
      id_pos_x_[second_id] = clampToDomain(parent_x + offset.x());
      id_pos_y_[second_id] = clampToDomain(parent_y + offset.y());
      id_pos_z_[second_id] = clampToDomain(parent_z + offset.z());
      index += 2;
    } else {
      ++index;
    }
  }
}

void SimulationEngine3DGlobal::mechanicalRelaxationStep() {
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

void SimulationEngine3DGlobal::takeStatSnapshot() {
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

void SimulationEngine3DGlobal::takePopulationSnapshot() {
  std::vector<CellEvoX::io::PopulationSnapshotRecord> snapshot;
  std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutation_payload;
  snapshot.reserve(spatial_state_.cell_ids.size());
  const auto payload_kind = config->full_mutation_payload
                                ? CellEvoX::io::MutationPayloadKind::Full
                                : CellEvoX::io::MutationPayloadKind::DriverOnly;

  for (size_t i = 0; i < spatial_state_.cell_ids.size(); ++i) {
    const uint32_t id = spatial_state_.cell_ids[i];
    CellMap::const_accessor accessor;
    if (!cells.find(accessor, id)) {
      continue;
    }

    const uint32_t mutation_payload_offset = static_cast<uint32_t>(mutation_payload.size());
    for (const auto& [mutation_id, mutation_type] : accessor->second.mutations) {
      const auto type_it = available_mutation_types.find(mutation_type);
      if (config->full_mutation_payload ||
          (type_it != available_mutation_types.end() && type_it->second.is_driver)) {
        mutation_payload.push_back({mutation_id, mutation_type});
      }
    }
    const auto mutation_payload_count =
        static_cast<uint16_t>(std::min<size_t>(mutation_payload.size() - mutation_payload_offset,
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
                        mutation_payload_count,
                        mutation_payload_offset,
                        1,
                        {0, 0, 0}});
  }

  const auto filename =
      CellEvoX::io::populationSnapshotPath(config->output_path, static_cast<int>(tau));
  if (!CellEvoX::io::writePopulationSnapshot(filename, tau, 3, snapshot, mutation_payload,
                                             payload_kind)) {
    spdlog::error("Failed to write population snapshot file: {}", filename);
  }
}

void SimulationEngine3DGlobal::pruneGraveyard() {
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

Eigen::Vector3f SimulationEngine3DGlobal::sampleRandomUnitVector(std::mt19937& rng) const {
  std::normal_distribution<float> normal_dist(0.0f, 1.0f);
  Eigen::Vector3f direction(normal_dist(rng), normal_dist(rng), normal_dist(rng));
  const float norm = direction.norm();
  if (norm <= 1e-6f) {
    return Eigen::Vector3f::UnitX();
  }
  return direction / norm;
}

float SimulationEngine3DGlobal::clampToDomain(float value) const {
  return std::clamp(value, 0.0f, config->spatial_domain_size);
}

void SimulationEngine3DGlobal::ensurePositionCapacity(uint32_t id) {
  if (id < id_pos_x_.size()) {
    return;
  }

  const size_t new_size = static_cast<size_t>(id) + 1;
  id_pos_x_.resize(new_size, 0.0f);
  id_pos_y_.resize(new_size, 0.0f);
  id_pos_z_.resize(new_size, 0.0f);
  id_to_spatial_index_.resize(new_size, kInvalidSpatialIndex);
}

size_t SimulationEngine3DGlobal::getRSS() {
  size_t rss = 0;
  std::ifstream statm("/proc/self/statm");
  if (statm.is_open()) {
    size_t ignored = 0;
    statm >> ignored >> rss;
  }

  const long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
  return rss * static_cast<size_t>(page_size_kb);
}

void SimulationEngine3DGlobal::logMemoryUsage() {
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
