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
#include "systems/CommonPopulationStep.hpp"
#include "systems/SimulationEngine.hpp"

class SimulationEngine3DCapacity {
 public:
  static constexpr float CELL_RADIUS = 1.0f;

  explicit SimulationEngine3DCapacity(std::shared_ptr<SimulationConfig> config);

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

  void initializePopulationPositions();
  void rebuildSpatialState();
  void assignBirthPositions(
      const std::vector<CellEvoX::systems::CommonBirthEvent>& births);
  void mechanicalRelaxationStep();
  void takeStatSnapshot();
  void takePopulationSnapshot();
  void pruneGraveyard();

  Eigen::Vector3f sampleRandomUnitVector(std::mt19937& rng) const;
  float clampToDomain(float value) const;
  void ensurePositionCapacity(uint32_t id);

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

  int last_stat_snapshot_tau = 0;
  int last_population_snapshot_tau = 0;
  int last_pruning_tau = -1;

  std::shared_ptr<SimulationConfig> config;
  std::mt19937 event_rng_;
  std::mt19937 spatial_rng_;

  SpatialState spatial_state_;
  SpatialHashGrid spatial_grid_;
  std::vector<float> id_pos_x_;
  std::vector<float> id_pos_y_;
  std::vector<float> id_pos_z_;
  std::vector<uint32_t> id_to_spatial_index_;

  std::ofstream memory_log_file;
};
