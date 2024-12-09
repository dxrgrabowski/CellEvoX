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
#include <tbb/blocked_range.h>
#include <tbb/tbb.h>
#include <iostream>

using namespace utils;

SimulationEngine::SimulationEngine(std::shared_ptr<SimulationConfig> config) :
    tau(0.0), 
    config(config), 
    actual_population(config->initial_population),
    total_deaths(0)
{
    cells.rehash(config->initial_population);
    
    for (uint32_t i = 0; i < config->initial_population; ++i) {
        cells.insert({i, Cell(i)});
    }

    for (const auto& mutation : config->mutations) {
        available_mutation_types[mutation.type_id] = mutation;
    }

    total_mutation_probability = std::accumulate(available_mutation_types.begin(), available_mutation_types.end(), 0.0, 
            [](double sum, const std::pair<const uint8_t, MutationType>& pair) {
                return sum + pair.second.probability;
            });
}

void SimulationEngine::step() {
    switch (config->sim_type) {
        case SimulationType::STOCHASTIC_TAU_LEAP:
            stochasticStep();
            break;
        // case SimulationType::DETERMINISTIC_RK4:
        //     deterministicStep();
        //     break;
    }
}

ecs::Run SimulationEngine::run(uint32_t steps) {
    auto last_update_time = std::chrono::steady_clock::now();
    const char* spinner = "|/-\\";
    int spinner_index = 0;
    const int bar_width = 50; // Width of the progress bar

    for (uint32_t i = 0; i < steps; ++i) {
        step();
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_update_time).count();
        if (elapsed_time >= 100) { // Update progress every 100 milliseconds
            int progress = static_cast<int>((static_cast<double>(i + 1) / steps) * 100);
            int pos = static_cast<int>((static_cast<double>(i + 1) / steps) * bar_width);

            std::cout << "\r\033[1;32mProgress: [\033[35m";
            for (int j = 0; j < bar_width; ++j) {
                if (j < pos) std::cout << "#";
                else std::cout << " ";
            }
            int steps_remaining = steps - (i + 1);
            std::cout << "\033[1;32m] " << progress << "% \033[34m" << spinner[spinner_index] 
                      << " \033[0m" << steps_remaining << " steps remaining" << std::flush;

            spinner_index = (spinner_index + 1) % 4;
            last_update_time = current_time;
        }
    }
    std::cout << "\r\033[1;32mProgress: [";
    for (int j = 0; j < bar_width; ++j) {
        std::cout << "#";
    }
    std::cout << "] 100% \033[0m" << std::endl;
    
    return ecs::Run(
        std::move(cells), 
        std::move(available_mutation_types),
        std::move(cells_graveyard),
        std::move(generational_stat_report),
        std::move(generational_popul_report),
        total_deaths, 
        tau
     );
}

void SimulationEngine::stop() {
    spdlog::info("Simulation stopped");
}

Eigen::VectorXd generateExponentialDistribution(int size) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::exponential_distribution<> exp_dist(1.0);
    
    Eigen::VectorXd result(size);
    for (int i = 0; i < size; ++i) {
        result(i) = exp_dist(gen);
    }
    
    return result;
}

void SimulationEngine::stochasticStep() {
    double tau_step = config->tau_step;
    tau += tau_step;
    const size_t N = actual_population;
    const size_t Nc = config->env_capacity;
    const double scaling_factor = static_cast<double>(N) / static_cast<double>(Nc);
   
    Eigen::VectorXd death_probs = 
    generateExponentialDistribution(N) / scaling_factor;
    std::vector<uint32_t> alive_cell_indices;
    for (auto it = cells.begin(); it != cells.end(); ++it) {
        alive_cell_indices.push_back(it->first);
    }

    Eigen::VectorXd birth_probs = generateExponentialDistribution(N).array() / FitnessCalculator::getCellsFitnessVector(cells, alive_cell_indices).array();
    
    if (alive_cell_indices.size() != N) {
        spdlog::error("Mismatch in alive cell count: expected {}, found {}", N, alive_cell_indices.size());
    }

    if( death_probs.size() != N || birth_probs.size() != N) {
        spdlog::error("Death arr: {} B: {} AP: {}", death_probs.size(), birth_probs.size(), N);
    }
    tbb::concurrent_vector<Cell> new_cells;
    tbb::concurrent_vector<uint32_t> dead_cells;
    std::atomic<uint32_t> new_cells_count(0), death_count(0);
    //printProbabilityVectors(death_probs, birth_probs);
   tbb::parallel_for(tbb::blocked_range<size_t>(0, alive_cell_indices.size()),
    [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i != range.end(); ++i) {
            //spdlog::info("Cell {} alive at {}", alive_cell_indices[i], i);
            uint32_t idx = alive_cell_indices[i];
            CellMap::accessor cell;
            if (cells.find(cell, idx)) {
                if (death_probs[i] <= tau_step) {
                    cells_graveyard.insert({cell->first, {cell->second.parent_id, tau}});
                    dead_cells.push_back(idx);
                    death_count++;
                    //spdlog::trace("Cell {} died", cells[i].id);
                } 
                else if (birth_probs[i] <= tau_step) {
                    new_cells_count += 2;

                    cells_graveyard.insert({cell->first, {cell->second.parent_id, tau}});
                    dead_cells.push_back(idx);
                    death_count++;
                    double rand_val = (Eigen::VectorXd::Random(1)(0) + 1.0) / 2.0; // Random val from 0.0 to 1.0   
                    if (rand_val >= total_mutation_probability) {
                        new_cells.emplace_back(
                            cell->second,
                            cell->second.fitness 
                        );
                        new_cells.emplace_back(
                            cell->second,
                            cell->second.fitness 
                        );
                    }else {
                        double prob_sum = 0.0;
                        for (const auto& mut : available_mutation_types) {
                            prob_sum += mut.second.probability;
                            if (rand_val < prob_sum) {
                                
                                Cell daughter_cell1 = Cell(
                                    cell->second,
                                    cell->second.fitness * (1.0 + mut.second.effect) 
                                );
                                daughter_cell1.mutations.push_back({0, mut.second.type_id});

                                new_cells.push_back(std::move(daughter_cell1));
                                new_cells.emplace_back(
                                    cell->second,
                                    cell->second.fitness 
                                );
                                break;
                            }
                        }
                    }
                }
            }
        }
    });
    
    auto starting_id = N + total_deaths;
    for (size_t i = 0; i < new_cells.size(); ++i) {
        CellMap::accessor accessor;
        new_cells[i].id = starting_id + i;
        if (!cells.insert(accessor, {starting_id + i, std::move(new_cells[i])}))
        {
            spdlog::error("Failed to insert new cell {}", starting_id + i);
        }
        
        for(auto &mut : accessor->second.mutations)
        {
            if(mut.first == 0)
                mut.first = starting_id + i;
        }
    }
    for (const auto& dead_id : dead_cells) {
        cells.erase(dead_id);
    }
    total_deaths += death_count;
    actual_population = actual_population + new_cells_count - death_count;
    
    int current_tau = static_cast<int>(tau);
    if (current_tau % config->stat_res == 0 && current_tau != last_stat_snapshot_tau) {
        takeStatSnapshot();
        last_stat_snapshot_tau = current_tau;
    }
    if (current_tau % config->popul_res == 0 && current_tau != last_population_snapshot_tau) {
        takePopulationSnapshot();
        last_population_snapshot_tau = current_tau;
    }
}

void SimulationEngine::takeStatSnapshot() {
    double total_fitness = 0.0;
    double total_fitness_squared = 0.0;
    double total_mutations = 0.0;
    double total_mutations_squared = 0.0;
    size_t living_cells_count = 0;

    for (const auto& cell : cells) {
        total_fitness += cell.second.fitness;
        total_fitness_squared += cell.second.fitness * cell.second.fitness;
        total_mutations += cell.second.mutations.size();
        total_mutations_squared += cell.second.mutations.size() * cell.second.mutations.size();
        ++living_cells_count;
    }

    double mean_fitness = total_fitness / living_cells_count;
    double fitness_variance = (total_fitness_squared / living_cells_count) - (mean_fitness * mean_fitness);

    double mean_mutations = total_mutations / living_cells_count;
    double mutations_variance = (total_mutations_squared / living_cells_count) - (mean_mutations * mean_mutations);

    generational_stat_report.push_back({tau ,mean_fitness, fitness_variance, mean_mutations, mutations_variance, living_cells_count});
}
 
void SimulationEngine::takePopulationSnapshot()
{
    CellMap cells_copy;
    cells_copy.rehash(cells.size());
    for (const auto& cell : cells) {
        CellMap::accessor accessor;
        cells_copy.insert(accessor, {cell.first, cell.second});
    }
    generational_popul_report.push_back({tau, std::move(cells_copy)});
}