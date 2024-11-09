#pragma once
#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>
#include "ecs/Cell.hpp"

class SimulationEngine {
public:
    enum class SimulationType {
        STOCHASTIC_TAU_LEAP,
        DETERMINISTIC_RK4
    };

    SimulationEngine(SimulationType type, double tau_step, size_t initial_population);
    
    void step();
    void run(size_t steps);
    
private:
    void stochasticStep();
    void deterministicStep();
    
    tbb::concurrent_vector<Cell, Cell::CellAllocator> cells;
    SimulationType sim_type;
    double tau;
};
