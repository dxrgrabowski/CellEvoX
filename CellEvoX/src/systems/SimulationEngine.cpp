#include "systems/SimulationEngine.hpp"
#include "utils/MathUtils.hpp"
#include "utils/SimulationConfig.hpp"
#include <execution>
#include <random>
#include <tbb/parallel_for.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/parallel_pipeline.h>
#include <Eigen/Dense>
#include <unordered_set>
using namespace utils;

SimulationEngine::SimulationEngine(SimulationConfig config) :
    tau(0.0), 
    config(config), 
    actual_population(config.initial_population),
    total_deaths(0)
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
    // Wyfiltrować wektor z martwych komórek, wartości mają być referencją, poprzez ustawianie stanu na DEAD będą one aplikować to w bazowym wektorze
    const size_t N = actual_population;
    const size_t Nc = config.env_capacity;
    
    Eigen::VectorXd death_probs = Eigen::VectorXd::Zero(N);
    Eigen::VectorXd birth_probs = Eigen::VectorXd::Zero(N);

    // OPTIMIZE: Try it later with batch version
    // Eigen::VectorXd fitness_vector = Eigen::VectorXd::Zero(N);
    // FitnessCalculator::updateFitnessVector(cells, fitness_vector);
    // Fast vectorized random generation
    death_probs = -death_probs.setRandom().array().log();  // Exponential distribution
    birth_probs = -birth_probs.setRandom().array().log();  // Exponential distribution

    std::vector<int> alive_cell_indices;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].state == Cell::State::ALIVE) {
            alive_cell_indices.push_back(i);
        }
    }
    
    if (alive_cell_indices.size() != N) {
        spdlog::error("Mismatch in alive cell count: expected {}, found {}", N, alive_cell_indices.size());
    }
    
    // Vectorized probability calculations
    death_probs = death_probs.array() / (N/Nc);
    birth_probs = birth_probs.array() / FitnessCalculator::getCellsFitnessVector(cells, alive_cell_indices).array();

    if( death_probs.size() != N || birth_probs.size() != N) {
        spdlog::error("Death arr: {} B: {} AP: {}", death_probs.size(), birth_probs.size(), N);
    }
    
    tbb::concurrent_vector<Cell> new_cells;
    std::atomic<int> new_cells_count(0), death_count(0);

    
    tbb::parallel_for(0, (int)alive_cell_indices.size(), [&](int idx) {
        int i = alive_cell_indices[idx];
        if (death_probs(idx) <= tau) {
            cells[i].state = Cell::State::DEAD;
            death_count++;
            //spdlog::trace("Cell {} died", cells[i].id);
        } 
        else if (birth_probs(idx) <= tau) {
            new_cells_count++;

            double rand_val = (Eigen::VectorXd::Random(1)(0) + 1.0) / 2.0 * total_mutation_probability;
            double prob_sum = 0.0;
                    
            for (const auto& mut : available_mutations) {
                prob_sum += mut.probability;
                if (rand_val < prob_sum) {
                    
                    Cell daughter_cell = Cell(
                    cells[i], 
                    0, // ID will be set later repair constructor
                    cells[i].fitness * (1.0 + mut.effect),
                        tau
                    );
                
                    daughter_cell.mutations.push_back(mut);
                    new_cells.push_back(daughter_cell);
                    //spdlog::trace("Cell {} mutated with {} : {}", cells[i].id, daughter_cell.id, toString(mut.type), mut.effect);
                    
                    if (daughter_cell.parent_id != cells[i].id) {
                        spdlog::error("Parent id mismatch: {} != {}", daughter_cell.parent_id, cells[i].id);
                    }
                    break;
                }
            }
        }
    });


    size_t starting_id = cells.size();
    size_t new_cells_size = new_cells.size();

    tbb::parallel_for(size_t(0), new_cells_size, [&](size_t i) {
        new_cells[i].id = starting_id + i;  // Każda komórka otrzymuje unikalny ID
        cells.push_back(new_cells[i]);
    });

    
    total_deaths += death_count;
    actual_population += new_cells_count;
    actual_population -= death_count;
    spdlog::info("Step {:.3f} for {} cells | New cells: {} | Dead cells: {}", tau, N, new_cells_count, death_count);

    // Sprawdzenie liczby martwych komórek
    size_t dead_cells_count = std::count_if(cells.begin(), cells.end(), [](const Cell& cell) {
        return cell.state == Cell::State::DEAD;
    });

    if (dead_cells_count != total_deaths) {
        spdlog::error("Post mismatch in dead cells count: expected {}, found {}", total_deaths, dead_cells_count);
    }

    // Sprawdzenie liczby narodzin
    size_t alive_cells_count = cells.size() - total_deaths;

    if (alive_cells_count != actual_population) {
        spdlog::error("Post mismatch in alive cells count: expected {}, found {}", actual_population, alive_cells_count);
    }

    // Sprawdzenie duplikatów i ciągłości ID
    std::unordered_set<uint64_t> cell_ids;
    bool duplicate_found = false;
    for (auto it = cells.begin(); it != cells.end(); ++it) {
        if (!cell_ids.insert(it->id).second) {
            spdlog::error("Duplicate cell ID found: {}", it->id);
            duplicate_found = true;
        }
    }

    // Sprawdzenie, czy ID odpowiadają liczbie komórek
    uint64_t max_id = 0;
    for (const auto& cell : cells) {
        if (cell.id > max_id) {
            max_id = cell.id;
        }
    }

    if (max_id + 1 != cells.size()) {
        spdlog::error("Mismatch in cell count and max ID: max ID {}, cell count {}", max_id, cells.size());
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
    // const double k1 = 0.1; // Growth rate
    // const double k2 = 0.05; // Death rate

    // tbb::parallel_for_each(cells.begin(), cells.end(),
    //     [&](Cell& cell) {
    //         // RK4 implementation for population dynamics
    //         double y = 1.0; // Initial population unit
    //         double t = 0.0;

    //         auto f = [k1, k2](double t, double y) {
    //             return k1 * y - k2 * y * y;
    //         };

    //         // RK4 steps
    //         double h = tau;
    //         double k1_rk = h * f(t, y);
    //         double k2_rk = h * f(t + h/2, y + k1_rk/2);
    //         double k3_rk = h * f(t + h/2, y + k2_rk/2);
    //         double k4_rk = h * f(t + h, y + k3_rk);

    //         double delta = (k1_rk + 2*k2_rk + 2*k3_rk + k4_rk) / 6;

    //         if (delta > 0) {
    //             cell.state = Cell::State::ALIVE; // AS
    //         } else if (delta < -0.5) {
    //             cell.state = Cell::State::DEAD;
    //         }
    //     }
    // );

    // // Handle population changes similar to stochastic method
    // tbb::concurrent_vector<Cell, Cell::CellMapAllocator> new_cells;
    // new_cells.reserve(cells.size() * 2);

    // for (const auto& cell : cells) {
    //     if (cell.state != Cell::State::DEAD) {
    //         new_cells.push_back(cell);
    //         if (1) {
    //             Cell daughter_cell = cell;
    //             daughter_cell.id = static_cast<uint64_t>(new_cells.size());
    //             new_cells.push_back(daughter_cell);
    //         }
    //     }
    // }

    // cells.swap(new_cells);
}
