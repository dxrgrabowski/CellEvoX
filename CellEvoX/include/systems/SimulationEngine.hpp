#pragma once
#include <tbb/concurrent_vector.h>
//#include <tbb/concurrent_hash_map.h>
#include "ecs/Cell.hpp"
#include "ecs/Run.hpp"

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
    void deterministicStep();
    //void rk4DeterministicStep(double deltaTime);
    tbb::concurrent_hash_map<uint32_t,Mutation> mutations;
    
    tbb::concurrent_vector<Cell> cells;
    std::vector<Mutation> available_mutations; // try tbb?
    size_t actual_population;
    size_t total_deaths;
    double tau;
    double total_mutation_probability;
    SimulationConfig config;
};
