#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <vector>

#include <Eigen/Dense>

#include "spatial/SpatialHashGrid.hpp"
#include "systems/SimulationEngine.hpp"

class SimulationEngine3D {
 public:
  static constexpr float CELL_RADIUS = 1.0f;

  explicit SimulationEngine3D(std::shared_ptr<SimulationConfig> config);

  static std::atomic<bool> shutdown_requested;
  static void signalHandler(int signum);

  ecs::Run run(uint32_t steps);
  void step();
  void stop();

 private:
  struct SpatialState {
    std::vector<uint32_t> cell_ids;
    std::vector<float> pos_x;
    std::vector<float> pos_y;
    std::vector<float> pos_z;
  };

  struct PendingDeath {
    uint32_t id;
    uint32_t parent_id;
  };

  struct PendingBirth {
    Cell cell;
    float x;
    float y;
    float z;
  };

  void initializePopulationPositions();
  void rebuildSpatialState();
  void stochasticStep3D();
  void mechanicalRelaxationStep();
  void takeStatSnapshot();
  void takePopulationSnapshot();
  void pruneGraveyard();

  Eigen::Vector3f sampleRandomUnitVector(std::mt19937& rng) const;
  float clampToDomain(float value) const;
  void ensurePositionCapacity(uint32_t id);
  std::mt19937& getThreadLocalRng() const;

  size_t getRSS();
  void logMemoryUsage();

  CellMap cells;
  Graveyard cells_graveyard;
  std::map<uint8_t, MutationType> available_mutation_types;
  std::vector<StatSnapshot> generational_stat_report;
  std::vector<std::pair<int, CellMap>> generational_popul_report;

  size_t actual_population;
  size_t total_deaths;
  double tau;
  double total_mutation_probability;
  uint32_t next_cell_id_;

  int last_stat_snapshot_tau = 0;
  int last_population_snapshot_tau = 0;
  int last_pruning_tau = -1;

  std::shared_ptr<SimulationConfig> config;
  std::mt19937 rng;

  SpatialState spatial_state_;
  SpatialHashGrid spatial_grid_;
  std::vector<float> id_pos_x_;
  std::vector<float> id_pos_y_;
  std::vector<float> id_pos_z_;
  std::vector<uint32_t> id_to_spatial_index_;

  std::ofstream memory_log_file;
};
