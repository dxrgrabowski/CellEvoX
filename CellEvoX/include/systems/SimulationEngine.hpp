#pragma once
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_vector.h>

#include <atomic>
#include <csignal>
#include <fstream>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include "ecs/Cell.hpp"
#include "ecs/Run.hpp"
#include "systems/DensePopulation.hpp"

using CellMap = tbb::concurrent_hash_map<uint32_t, Cell>;
using Graveyard = tbb::concurrent_hash_map<uint32_t, std::pair<uint32_t, double>>;
enum class SimulationType {
  STOCHASTIC_TAU_LEAP,
  SPATIAL_3D_CAPACITY
};

struct SimulationConfig {
  SimulationType sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
  double tau_step = 0.005;
  uint32_t seed = 42;
  size_t initial_population = 0;
  size_t env_capacity = 0;
  size_t steps = 0;
  uint32_t stat_res = 1;
  uint32_t popul_res = 1;
  int graveyard_pruning_interval = 0;
  size_t max_population_cutoff = 0;  // 0 = disabled; stop when N >= this value
  std::string output_path;
  std::vector<MutationType> mutations;
  bool full_mutation_payload = true;
  int verbosity = 2; // 0: off, 1: minimal, 2: full
  uint32_t phylogeny_num_cells_sampling = 100;
  float spatial_domain_size = 200.0f;
  float spring_constant = 0.5f;
  float mech_dt = 0.1f;
  int mech_substeps = 5;
  float epsilon = 0.1f;
  // A realized mutation effect with |effect| at or below this band is reported
  // as neutral. One float epsilon (~1.19e-7) is the gap between 1.0 and the next
  // representable float, the resolution of `Cell::fitness`. This is a reporting
  // threshold, not proof that fitness did not change. Raise it to treat small
  // effects as drift on purpose (see ADR-0012, "Neutral band").
  double mutation_effect_neutral_tolerance = std::numeric_limits<float>::epsilon();
};

// Mutation probabilities are stored as `float`, so summing them (in `double`)
// can overshoot 1 by a few float ulps: 0.2f + 0.8f == 1.0000000149. The config
// validator, the runtime guard in CommonPopulationStep and the HTTP API all use
// this slack so that a pool that sums to exactly 1 in decimal is accepted by
// every layer instead of being rejected by one of them.
inline constexpr double kTotalMutationProbabilityTolerance = 1e-5;

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
  void runSteps(uint32_t steps);
  void stochasticStep();
  void pruneGraveyard();
  void takeStatSnapshot();
  void takePopulationSnapshot();
  CellEvoX::systems::DensePopulation population;
  // <id, <parent_id, death_time>>
  Graveyard cells_graveyard;
  std::map<uint8_t, MutationType> available_mutation_types;
  std::vector<StatSnapshot> generational_stat_report;
  std::vector<std::pair<int, CellMap>> generational_popul_report;
  size_t actual_population;
  size_t total_deaths;
  double tau;
  double total_mutation_probability;
  double total_mutation_rate;
  int last_stat_snapshot_tau = 0;
  int last_population_snapshot_tau = 0;
  int last_memory_log_tau = 0;
  int last_pruning_tau = -1;
  ecs::TerminationReason termination_reason = ecs::TerminationReason::PlannedHorizon;
  std::shared_ptr<SimulationConfig> config;
  
  std::ofstream memory_log_file;
  void logMemoryUsage();
  size_t getRSS();
};
