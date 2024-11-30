#pragma once
#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_hash_map.h>
#include "ecs/Cell.hpp"
#include "ecs/Run.hpp"

using CellMap = tbb::concurrent_hash_map<uint32_t, Cell>;

enum class SimulationType {
    STOCHASTIC_TAU_LEAP,
    DETERMINISTIC_RK4
};

struct SimulationConfig {
        std::vector<Mutation> mutations;
        SimulationType sim_type;
        double tau_step;
        size_t initial_population;
        size_t env_capacity;
        size_t steps;
    };

class SimulationEngine {
public:

    SimulationEngine(SimulationConfig config);
    
    void step();
    const ecs::Run run(uint32_t steps);
    void stop();
    
private:
    void stochasticStep();
    //void rk4DeterministicStep(double deltaTime);
    tbb::concurrent_hash_map<uint32_t, tbb::concurrent_vector<uint32_t>> mutation_instances;

    CellMap cells;
    // <id, <parent_id, death_time>>
    tbb::concurrent_hash_map<uint32_t, std::pair<uint32_t, double>> cells_graveyard; 
    std::unordered_map<uint8_t, Mutation> available_mutation_types; // try tbb?
    size_t actual_population;
    size_t total_deaths;
    double tau;
    double total_mutation_probability;
    SimulationConfig config;
};
