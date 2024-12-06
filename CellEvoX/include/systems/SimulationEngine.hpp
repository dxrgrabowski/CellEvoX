#pragma once
#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_hash_map.h>
#include "ecs/Cell.hpp"
#include "ecs/Run.hpp"

using CellMap = tbb::concurrent_hash_map<uint32_t, Cell>;
using Graveyard = tbb::concurrent_hash_map<uint32_t, std::pair<uint32_t, double>>;
enum class SimulationType {
    STOCHASTIC_TAU_LEAP,
    DETERMINISTIC_RK4
};

struct SimulationConfig {
    SimulationType sim_type;
    double tau_step;
    size_t initial_population;
    size_t env_capacity;
    size_t steps;
    uint32_t stat_res;
    uint32_t popul_res;
    std::vector<MutationType> mutations;
};

struct StatSnapshot {
    double tau;
    double mean_fitness;
    double fitness_variance;
    size_t total_living_cells;
};

class SimulationEngine {
public:

    SimulationEngine(std::shared_ptr<SimulationConfig>);
    
    void step();
    ecs::Run run(uint32_t steps);
    void stop();
    
private:
    void stochasticStep();
    //void rk4DeterministicStep(double deltaTime);
    void takeStatSnapshot();
    void takePopulationSnapshot();
    CellMap cells;
    // <id, <parent_id, death_time>>
    Graveyard cells_graveyard; 
    std::unordered_map<uint8_t, MutationType> available_mutation_types;
    std::vector<StatSnapshot> generational_stat_report;
    std::vector<CellMap> generational_popul_report;
    size_t actual_population;
    size_t total_deaths;
    double tau;
    double total_mutation_probability;
    int last_stat_snapshot_tau = -1;
    int last_population_snapshot_tau = -1;
    std::shared_ptr<SimulationConfig> config;
};
