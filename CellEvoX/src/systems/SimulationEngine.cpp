#include "systems/SimulationEngine.hpp"
#include <random>
#include <tbb/parallel_for_each.h>

SimulationEngine::SimulationEngine(SimulationType type, double tau_step, size_t initial_population)
    : sim_type(type), tau(tau_step) {
    cells.reserve(initial_population);
    
    for (size_t i = 0; i < initial_population; ++i) {
        cells.push_back(Cell{});
    }
}

void SimulationEngine::step() {
    switch (sim_type) {
        case SimulationType::STOCHASTIC_TAU_LEAP:
            stochasticStep();
            break;
        case SimulationType::DETERMINISTIC_RK4:
            deterministicStep();
            break;
    }
}

void SimulationEngine::run(size_t steps) {
    for (size_t i = 0; i < steps; ++i) {
        step();
    }
}

void SimulationEngine::stochasticStep() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    tbb::parallel_for_each(cells.begin(), cells.end(), 
        [&](Cell& cell) {
            double probability = dis(gen);
            
            if (probability < 0.2) { // Death probability
                cell.state = Cell::State::DEAD;
            } 
            else if (probability < 0.4) { // Split probability
                cell.state = Cell::State::ALIVE_SPLITTING;
                
                // Handle mutations
                if (dis(gen) < 0.1) { // Mutation probability
                    Mutation new_mutation{
                        .effect = dis(gen),
                        .probability = dis(gen),
                        .id = static_cast<uint32_t>(dis(gen) * 1000)
                    };
                    cell.mutations.push_back(new_mutation);
                }
            }
        }
    );

    // Handle cell division and cleanup
    tbb::concurrent_vector<Cell, Cell::CellAllocator> new_cells;
    new_cells.reserve(cells.size() * 2);

    for (const auto& cell : cells) {
        if (cell.state != Cell::State::DEAD) {
            new_cells.push_back(cell);
            if (cell.state == Cell::State::ALIVE_SPLITTING) {
                Cell daughter_cell = cell;
                daughter_cell.id = static_cast<uint64_t>(new_cells.size());
                new_cells.push_back(daughter_cell);
            }
        }
    }

    cells.swap(new_cells);
}

void SimulationEngine::deterministicStep() {
    const double k1 = 0.1; // Growth rate
    const double k2 = 0.05; // Death rate

    tbb::parallel_for_each(cells.begin(), cells.end(),
        [&](Cell& cell) {
            // RK4 implementation for population dynamics
            double y = 1.0; // Initial population unit
            double t = 0.0;

            auto f = [k1, k2](double t, double y) {
                return k1 * y - k2 * y * y;
            };

            // RK4 steps
            double h = tau;
            double k1_rk = h * f(t, y);
            double k2_rk = h * f(t + h/2, y + k1_rk/2);
            double k3_rk = h * f(t + h/2, y + k2_rk/2);
            double k4_rk = h * f(t + h, y + k3_rk);

            double delta = (k1_rk + 2*k2_rk + 2*k3_rk + k4_rk) / 6;

            if (delta > 0) {
                cell.state = Cell::State::ALIVE_SPLITTING;
            } else if (delta < -0.5) {
                cell.state = Cell::State::DEAD;
            }
        }
    );

    // Handle population changes similar to stochastic method
    tbb::concurrent_vector<Cell, Cell::CellAllocator> new_cells;
    new_cells.reserve(cells.size() * 2);

    for (const auto& cell : cells) {
        if (cell.state != Cell::State::DEAD) {
            new_cells.push_back(cell);
            if (cell.state == Cell::State::ALIVE_SPLITTING) {
                Cell daughter_cell = cell;
                daughter_cell.id = static_cast<uint64_t>(new_cells.size());
                new_cells.push_back(daughter_cell);
            }
        }
    }

    cells.swap(new_cells);
}
