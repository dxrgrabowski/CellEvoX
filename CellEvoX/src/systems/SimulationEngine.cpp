#include "systems/SimulationEngine.hpp"
#include "utils/MathUtils.hpp"
#include "utils/SimulationConfig.hpp"
#include <random>
#include <tbb/parallel_for_each.h>
#include <Eigen/Dense>

using namespace utils;

SimulationEngine::SimulationEngine(SimulationConfig config) :
    tau(0.0), 
    config(config)
{
    utils::printConfig(config);
    cells.reserve(config.initial_population);
    
    for (size_t i = 0; i < config.initial_population; ++i) {
        cells.emplace_back(i);
    }

    available_mutations.assign(config.mutations.begin(), config.mutations.end());

    total_mutation_probability = std::accumulate(available_mutations.begin(), available_mutations.end(), 0.0, 
            [](double sum, const Mutation& mutation) {
                return sum + mutation.probability;
            });

    //spdlog::info("Running simulation for {} steps", config.steps);
    //run(config.steps);
}

void SimulationEngine::step() {
    spdlog::debug("Simulation step {}", tau);
    switch (config.sim_type) {
        case SimulationType::STOCHASTIC_TAU_LEAP:
            stochasticStep();
            break;
        case SimulationType::DETERMINISTIC_RK4:
            deterministicStep();
            break;
    }
}

void SimulationEngine::run(size_t steps) {
    auto start_time = std::chrono::steady_clock::now();
    for (size_t i = 0; i < steps; ++i) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        
        if (elapsed_time >= 30) {
            spdlog::info("Simulation ended after {} seconds at step {}", elapsed_time, i);
            break;
        }
        
        step();
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, cells.size() - 1);

    size_t total_mutations = 0;
    size_t driver_mutations = 0;
    size_t positive_mutations = 0;
    size_t neutral_mutations = 0;
    size_t negative_mutations = 0;

    for (size_t i = 0; i < cells.size(); ++i) {
        size_t random_index = dis(gen);
        total_mutations += cells[random_index].mutations.size();
        for (const auto& mutation : cells[random_index].mutations) {
            switch (mutation.type) {
                case MutationType::DRIVER:
                    ++driver_mutations;
                    break;
                case MutationType::POSITIVE:
                    ++positive_mutations;
                    break;
                case MutationType::NEUTRAL:
                    ++neutral_mutations;
                    break;
                case MutationType::NEGATIVE:
                    ++negative_mutations;
                    break;
            }
        }
    }

    double average_mutations = static_cast<double>(total_mutations) / cells.size();
    size_t mutation_vector_size = average_mutations * sizeof(Mutation);
    size_t total_mutation_memory = mutation_vector_size * cells.size();

    spdlog::info("Average number of mutations per cell: {:.2f}", average_mutations);
    spdlog::info("Estimated total memory for mutations: {} bytes", total_mutation_memory);


    spdlog::info("Simulation ended at time {}", tau);
    spdlog::info("Total cells: {}\n", cells.size());
    spdlog::info("Size of one Cell: {} bytes", sizeof(Cell));
    size_t total_size = cells.size() * sizeof(Cell);
    size_t total_mutation_size = total_size / (1024 * 1024);
    spdlog::info("Total size of Cells: {} B | {} MB", total_size, total_mutation_size);
    spdlog::info("Total mutations: {} size: {} B | {} MB", total_mutations, total_mutation_memory, total_mutation_memory / (1024 * 1024));  
    spdlog::info("Total memory: {} B | {} MB\n", total_size + total_mutation_memory, (total_size + total_mutation_memory) / (1024 * 1024));

    spdlog::info("Driver mutations: {}", driver_mutations);
    spdlog::info("Positive mutations: {}", positive_mutations);
    spdlog::info("Neutral mutations: {}", neutral_mutations);
    spdlog::info("Negative mutations: {}", negative_mutations);
    spdlog::info("Total mutations: {}", total_mutations);
}

void SimulationEngine::stop() {
    spdlog::info("Simulation stopped");
}

void SimulationEngine::stochasticStep() {
    tau += config.tau_step;
    
    const size_t N = cells.size();
    const size_t Nc = config.env_capacity;
    
    Eigen::VectorXd death_probs = Eigen::VectorXd::Zero(N);
    Eigen::VectorXd birth_probs = Eigen::VectorXd::Zero(N);

    // Try it later with batch version
    // Eigen::VectorXd fitness_vector = Eigen::VectorXd::Zero(N);
    // FitnessCalculator::updateFitnessVector(cells, fitness_vector);
    spdlog::debug("Calculating probabilities for {} cells", N);
    // Fast vectorized random generation
    death_probs = -death_probs.setRandom().array().log();  // Exponential distribution
    birth_probs = -birth_probs.setRandom().array().log();  // Exponential distribution
    
    // Vectorized probability calculations
    death_probs = death_probs.array() / (N/Nc);
    birth_probs = birth_probs.array() / FitnessCalculator::getCellsFitnessVector(cells).array();

    tbb::concurrent_vector<Cell, Cell::CellAllocator> new_cells;
    new_cells.reserve(cells.size() * 1.5);
    int new_cells_count = 0;

    tbb::parallel_for(size_t(0), cells.size(), [&](size_t i) {
        if (death_probs(i) <= tau) {
            cells[i].state = Cell::State::DEAD;
            //spdlog::trace("Cell {} died", cells[i].id);
        } 
        else if (birth_probs(i) <= tau) {
            new_cells_count++;
            cells[i].state = Cell::State::ALIVE;

            double rand_val = (Eigen::VectorXd::Random(1)(0) + 1.0) / 2.0 * total_mutation_probability;
            double prob_sum = 0.0;
                    
            for (const auto& mut : available_mutations) {
                prob_sum += mut.probability;
                if (rand_val < prob_sum) {

                    Cell daughter_cell = Cell(
                        cells[i], 
                        new_cells_count,
                        cells[i].fitness * (1.0 + mut.effect),
                        tau
                    );
                    
                    daughter_cell.mutations.push_back(mut);
                    new_cells.push_back(daughter_cell);
                    //spdlog::trace("Cell {} -> {} mutated with {} : {}", cells[i].id, daughter_cell.id,toString(mut.type), mut.effect);

                    if (daughter_cell.parent_id != cells[i].id) {
                        spdlog::error("Parent id mismatch: {} != {}", daughter_cell.parent_id, cells[i].id);
                    }
                    break;
                }
            }
        }
    });

    for (const auto& cell : new_cells) {
        cells.push_back(cell);
    }
}

// void SimulationEngine::rk4DeterministicStep(double deltaTime) {
//     const size_t N = cells.size();
//     const size_t Nc = config.env_capacity;
//     double time_step = deltaTime;
//     Eigen::VectorXd birth_rates = Eigen::VectorXd::Zero(N);
//     Eigen::VectorXd death_rates = Eigen::VectorXd::Zero(N);

//     // Calculate base birth and death rates based on fitness and capacity
//     Eigen::VectorXd fitness_vector = FitnessCalculator::getCellsFitnessVector(cells);
//     birth_rates = fitness_vector.array() / Nc;
//     death_rates = Eigen::VectorXd::Ones(N) / Nc;

//     // Intermediate steps for RK4
//     Eigen::VectorXd k1, k2, k3, k4;

//     // Function to calculate the rates of change for birth and death rates
//     auto rate_function = [&](const Eigen::VectorXd& state) {
//         Eigen::VectorXd birth_term = birth_rates.array() * state.array();
//         Eigen::VectorXd death_term = death_rates.array() * state.array();
//         return birth_term - death_term; // Population change due to birth and death
//     };

//     // Compute RK4 intermediate steps
//     k1 = rate_function(cells);
//     k2 = rate_function(cells + 0.5 * time_step * k1);
//     k3 = rate_function(cells + 0.5 * time_step * k2);
//     k4 = rate_function(cells + time_step * k3);

//     // Update cell populations using RK4 method
//     Eigen::VectorXd delta_cells = (time_step / 6.0) * (k1 + 2 * k2 + 2 * k3 + k4);
//     cells += delta_cells;

//     // Apply deterministic mutations to cells based on mutation probabilities
//     for (size_t i = 0; i < cells.size(); ++i) {
//         double mutation_probability = total_mutation_probability;
//         double rand_val = Eigen::VectorXd::Random(1)(0);

//         if (rand_val < mutation_probability) {
//             for (const auto& mut : available_mutations) {
//                 if (rand_val < mut.probability) {
//                     cells[i].fitness *= (1.0 + mut.effect); // Apply mutation effect
//                     cells[i].mutations.push_back(mut);       // Record mutation
//                     break;
//                 }
//             }
//         }
//     }

//     // Ensure that no population values are negative
//     cells = cells.cwiseMax(0);
// }


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
                cell.state = Cell::State::ALIVE; // AS
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
            if (1) {
                Cell daughter_cell = cell;
                daughter_cell.id = static_cast<uint64_t>(new_cells.size());
                new_cells.push_back(daughter_cell);
            }
        }
    }

    cells.swap(new_cells);
}
