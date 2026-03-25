#pragma once
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_vector.h>

#include <atomic>
#include <csignal>
#include <fstream>

#include "ecs/Cell.hpp"
#include "ecs/Run.hpp"
#include "systems/SpatialHashGrid.hpp"

using CellMap = tbb::concurrent_hash_map<uint32_t, Cell>;
using Graveyard = tbb::concurrent_hash_map<uint32_t, std::pair<uint32_t, double>>;
enum class SimulationType { STOCHASTIC_TAU_LEAP, DETERMINISTIC_RK4 };

struct SimulationConfig {
  SimulationType sim_type;
  double tau_step;
  size_t initial_population;
  size_t env_capacity;
  size_t steps;
  uint32_t stat_res;
  uint32_t popul_res;
  int graveyard_pruning_interval;
  std::string output_path;
  std::vector<MutationType> mutations;
  int verbosity; // 0: off, 1: minimal, 2: full
  uint32_t phylogeny_num_cells_sampling;

  // --- 3D Spatial ABM parameters ---
  float max_local_density{10.0f};
  float sample_radius{3.0f * Cell::CELL_RADIUS};
  float mech_dt{0.1f};
  int mech_iterations{5};
  float spawn_offset{0.1f * Cell::CELL_RADIUS};
};

/// Packed binary snapshot for high-performance I/O.
/// Size: 4+4+4+4+4+4+1 = 25 bytes per cell (no padding).
#pragma pack(push, 1)
struct CellSnapshotBinary {
  uint32_t id;
  uint32_t parent_id;
  float fitness;
  float x;
  float y;
  float z;
  uint8_t mutations_count;
};
#pragma pack(pop)

struct StatSnapshot {
  double tau;
  double mean_fitness;
  double fitness_variance;
  double mean_mutations;
  double mutations_variance;
  size_t total_living_cells;
  double fitness_skewness;
  double fitness_kurtosis;
  double mutations_skewness;
  double mutations_kurtosis;
};

class SimulationEngine {
 public:
  SimulationEngine(std::shared_ptr<SimulationConfig>);

  static std::atomic<bool> shutdown_requested;
  static void signalHandler(int signum);

  void step();
  ecs::Run run(uint32_t steps);
  void stop();

 private:
  void stochasticStep();
  void mechanicalRelaxationStep();
  void pruneGraveyard();
  void takeStatSnapshot();
  void takePopulationSnapshot();
  void writeBinarySnapshot(const std::string& path);

  CellMap cells;
  Graveyard cells_graveyard;
  std::unordered_map<uint8_t, MutationType> available_mutation_types;
  std::vector<StatSnapshot> generational_stat_report;

  // --- Spatial data structures ---
  SpatialHashGrid grid;
  /// Contiguous position buffer for the current step (read-only during force calc).
  std::vector<Eigen::Vector3f> positions_read;
  /// Write buffer for new positions (each element written by exactly one thread).
  std::vector<Eigen::Vector3f> positions_write;
  /// Contiguous array of active cell IDs (rebuilt each step).
  std::vector<uint32_t> active_cell_ids;
  /// Fitness values extracted in single-pass with positions (avoids 2nd CellMap traversal).
  std::vector<double> fitness_buf_;
  /// Local density per cell (reused across sub-steps).
  std::vector<float> local_density_buf_;

  size_t actual_population;
  size_t total_deaths;
  double tau;
  double total_mutation_probability;
  int last_stat_snapshot_tau = 0;
  int last_population_snapshot_tau = 0;
  int last_pruning_tau = -1;
  std::shared_ptr<SimulationConfig> config;

  std::ofstream memory_log_file;
  void logMemoryUsage();
  size_t getRSS();
};
