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

SimulationEngine::SimulationEngine(SimulationConfig config) :
    tau(0.0), 
    config(config), 
    actual_population(config.initial_population),
    total_deaths(0)
{
    utils::printConfig(config);
    cells.rehash(config.initial_population);
    
    for (uint32_t i = 0; i < config.initial_population; ++i) {
        cells.insert({i, Cell(i)});
    }

    for (const auto& mutation : config.mutations) {
        available_mutation_types[mutation.id] = mutation;
    }

    total_mutation_probability = std::accumulate(available_mutation_types.begin(), available_mutation_types.end(), 0.0, 
            [](double sum, const std::pair<const uint8_t, Mutation>& pair) {
                return sum + pair.second.probability;
            });

    //spdlog::info("Running simulation for {} steps", config.steps);
    //run(config.steps);
}

void SimulationEngine::step() {
    switch (config.sim_type) {
        case SimulationType::STOCHASTIC_TAU_LEAP:
            stochasticStep();
            break;
        // case SimulationType::DETERMINISTIC_RK4:
        //     deterministicStep();
        //     break;
    }
}

const ecs::Run SimulationEngine::run(uint32_t steps) {
    auto start_time = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < steps; ++i) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        
        if (elapsed_time >= 60) {
            spdlog::info("Simulation ended after {} seconds at step {}", elapsed_time, i);
            break;
        }
        
        step();
    }
    
    return ecs::Run(std::move(cells), 
        std::move(available_mutation_types),
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
    tau += config.tau_step;
    const size_t N = actual_population;
    const size_t Nc = config.env_capacity;
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
                if (death_probs[i] <= config.tau_step) {
                    cells_graveyard.insert({cell->first, {cell->second.parent_id, tau}});
                    dead_cells.push_back(idx);
                    death_count++;
                    //spdlog::trace("Cell {} died", cells[i].id);
                } 
                else if (birth_probs[i] <= config.tau_step) {
                    new_cells_count += 2;

                    cells_graveyard.insert({cell->first, {cell->second.parent_id, tau}});
                    dead_cells.push_back(idx);
                    death_count++;
                    double rand_val = (Eigen::VectorXd::Random(1)(0) + 1.0) / 2.0; // Random val from 0.0 to 1.0   
                    if (rand_val >= total_mutation_probability) {
                        new_cells.push_back(Cell(
                            cell->second,
                            0,
                            cell->second.fitness 
                        ));
                        new_cells.push_back(Cell(
                            cell->second,
                            0,
                            cell->second.fitness 
                        ));
                    }else {
                        double prob_sum = 0.0;
                        for (const auto& mut : available_mutation_types) {
                            prob_sum += mut.second.probability;
                            if (rand_val < prob_sum) {
                                
                                Cell daughter_cell1 = Cell(
                                    cell->second,
                                    0,
                                    cell->second.fitness * (1.0 + mut.second.effect) 
                                );
                                daughter_cell1.mutations.push_back(mut.second.id);

                                new_cells.push_back(std::move(daughter_cell1));
                                new_cells.push_back(Cell(
                                    cell->second,
                                    0,
                                    cell->second.fitness 
                                ));
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
        cells.insert(accessor, {starting_id + i, std::move(new_cells[i])});
    }
    for (const auto& dead_id : dead_cells) {
        cells.erase(dead_id);
    }

    total_deaths += death_count;
    actual_population = actual_population + new_cells_count - death_count;
    // spdlog::info("Step {:.3f} {} -> {} cells | New cells: {} | Dead cells: {} Total deaths: {}", tau,N, actual_population, new_cells_count, death_count, total_deaths);

    // if (cells.size() != actual_population) {
    //     spdlog::error("Post Mismatch in cell count: expected {}, found {}", actual_population, cells.size());
    // }
    // if (total_deaths != cells_graveyard.size()) {
    //     spdlog::error("Post Mismatch in graveyard count: expected {}, found {}", total_deaths, cells_graveyard.size());
    // }
}

