#include "systems/SimulationEngine.hpp"
#include "utils/MathUtils.hpp"
#include "utils/SimulationConfig.hpp"
#include <random>
#include <tbb/parallel_for_each.h>
#include <Eigen/Dense>

using namespace utils;

SimulationEngine::SimulationEngine(SimulationConfig config)
    : sim_type(config.sim_type), 
    tau(config.tau_step), 
    env_capacity(config.env_capacity)
{
    utils::printConfig(config);
    cells.reserve(config.initial_population);
    
    for (size_t i = 0; i < config.initial_population; ++i) {
        cells.emplace_back(i);
    }

    available_mutations.assign(config.mutations.begin(), config.mutations.end());
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
    const size_t N = cells.size();
    const size_t Nc = env_capacity;
    
    Eigen::VectorXd death_probs = Eigen::VectorXd::Zero(N);
    Eigen::VectorXd birth_probs = Eigen::VectorXd::Zero(N);

    // Try it later with batch version
    // Eigen::VectorXd fitness_vector = Eigen::VectorXd::Zero(N);
    // FitnessCalculator::updateFitnessVector(cells, fitness_vector);
    
    // Fast vectorized random generation
    death_probs = -death_probs.setRandom().array().log();  // Exponential distribution
    birth_probs = -birth_probs.setRandom().array().log();  // Exponential distribution
    
    // Vectorized probability calculations
    death_probs = death_probs.array() / (N/Nc);
    birth_probs = birth_probs.array() / FitnessCalculator::getCellsFitnessVector(cells).array();

    size_t new_cells_needed = 0;

    tbb::parallel_for(size_t(0), cells.size(), [&](size_t i) {
        if (death_probs(i) <= tau) {
            cells[i].state = Cell::State::DEAD;
        } 
        else if (birth_probs(i) <= tau) {
            cells[i].state = Cell::State::ALIVE_SPLITTING;
            new_cells_needed++;
        }
    });

    // cells.erase(std::remove_if(cells.begin(), cells.end(),
    //     [](const Cell& cell) { return cell.state == Cell::State::DEAD; }),
    //     cells.end());


    // tbb::parallel_for(size_t(0), cells.size(), [&](size_t i) {
    //     if (birth_probs(i) <= tau && !mutationOccurred(i)) {
    //         // Copy cell
    //         tempCells.push_back(cells[i].copy());
    //     } else if (birth_probs(i) <= tau && mutationOccurred(i)) {
    //         // Mutate and copy cell
    //         tempCells.push_back(cells[i].copyAndMutate());
    //     }
    // });
    // std::binomial_distribution<> mut_dist(1, mutation_probability);
    // std::mt19937 gen(std::random_device{}());

    // for (const auto& cell : cells) {
    //     if (cell.state != Cell::State::DEAD) {
    //         new_cells.push_back(cell);
            
    //         if (cell.state == Cell::State::ALIVE_SPLITTING) {
    //             Cell daughter_cell = cell;
    //             daughter_cell.id = static_cast<uint64_t>(new_cells.size());
                
    //             if (mut_dist(gen)) {
    //                 applyMutation(daughter_cell);
    //             }
                
    //             new_cells.push_back(daughter_cell);
    //         }
    //     }
    // }

    // cells.swap(new_cells);
}


// Nice to have feature for the future
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
