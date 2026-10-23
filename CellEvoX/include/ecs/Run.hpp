#pragma once

#include <spdlog/spdlog.h>
#include <tbb/concurrent_hash_map.h>

#include <random>
#include <map>
#include <unordered_set>
#include <vector>

#include "ecs/Cell.hpp"

struct StatSnapshot;
namespace ecs {
enum class TerminationReason {
  PlannedHorizon,
  PopulationCutoff,
  ShutdownRequested,
};

inline const char* terminationReasonName(TerminationReason reason) {
  switch (reason) {
    case TerminationReason::PlannedHorizon:
      return "planned_horizon";
    case TerminationReason::PopulationCutoff:
      return "population_cutoff";
    case TerminationReason::ShutdownRequested:
      return "shutdown_requested";
  }
  return "unknown";
}

struct NodeData {
  uint32_t parent_id;
  uint32_t child_sum = 0;
  double death_time;
};
using CellMap = tbb::concurrent_hash_map<uint32_t, Cell>;
using Graveyard = tbb::concurrent_hash_map<uint32_t, std::pair<uint32_t, double>>;
class Run {
 public:
  CellMap cells;
  std::map<uint8_t, MutationType> mutation_id_to_type;
  tbb::concurrent_hash_map<uint32_t, NodeData> phylogenetic_tree;
  Graveyard cells_graveyard;
  std::vector<StatSnapshot> generational_stat_report;
  std::vector<std::pair<int, CellMap>> generational_popul_report;
  size_t total_deaths = 0;
  size_t total_mutations = 0;
  int driver_mutations = 0;
  int positive_mutations = 0;
  int neutral_mutations = 0;
  int negative_mutations = 0;
  float average_mutations = 0.0;
  size_t total_cell_memory_usage = 0;
  size_t total_mutations_memory = 0;
  size_t total_graveyard_memory = 0;
  double tau = 0.0;
  TerminationReason termination_reason = TerminationReason::PlannedHorizon;

  Run(CellMap &&cells,
      std::map<uint8_t, MutationType> mutation_id_to_type,
      Graveyard &&cells_graveyard,
      std::vector<StatSnapshot> &&generational_stat_report,
      std::vector<std::pair<int, CellMap>> generational_popul_report,
      size_t deaths,
      double tau,
      TerminationReason termination_reason = TerminationReason::PlannedHorizon);

  Run(const Run &) = delete;
  Run &operator=(const Run &) = delete;

  Run(Run &&) = default;
  Run &operator=(Run &&) = default;

  void processRunInfo();
  void logResults() const;
  void createPhylogeneticTree();
  void checkRunCorrectness() const;
};

}  // namespace ecs
