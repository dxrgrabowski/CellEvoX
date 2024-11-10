#pragma once
#include <tbb/concurrent_vector.h>
#include "ecs/Cell.hpp"

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
    void run(size_t steps);
    
private:
    void stochasticStep();
    void deterministicStep();
    
    tbb::concurrent_vector<Cell, Cell::CellAllocator> cells;
    tbb::concurrent_vector<Mutation> available_mutations;
    size_t env_capacity;
    SimulationType sim_type;
    double tau;
};
