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

const ecs::Run SimulationEngine::run(uint32_t steps) {
    auto start_time = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < steps; ++i) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        
        if (elapsed_time >= 10) {
            spdlog::info("Simulation ended after {} seconds at step {}", elapsed_time, i);
            break;
        }
        
        step();
    }

    return ecs::Run(std::move(cells), 
        total_deaths, 
        tau
    );
}

void SimulationEngine::stop() {
    spdlog::info("Simulation stopped");
}

void SimulationEngine::stochasticStep() {
    tau += config.tau_step;
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

    std::vector<uint32_t> alive_cell_indices;
    for (auto it = cells.begin(); it != cells.end(); ++it) {
        if (it->second.state == Cell::State::ALIVE) {
            alive_cell_indices.push_back(it->first);
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
    tbb::concurrent_vector<uint32_t> dead_cells;
    std::atomic<uint32_t> new_cells_count(0), death_count(0);

   tbb::parallel_for(tbb::blocked_range<size_t>(0, alive_cell_indices.size()),
    [&](const tbb::blocked_range<size_t>& range) {
        for (size_t i = range.begin(); i != range.end(); ++i) {
            //spdlog::info("Cell {} alive at {}", alive_cell_indices[i], i);
            uint32_t idx = alive_cell_indices[i];
            CellMap::accessor cell;
            if (cells.find(cell, idx)) {
                if (death_probs(i) <= tau) {
                    cell->second.state = Cell::State::DEAD;
                    cells_graveyard.insert({cell->first, {cell->second.parent_id, tau}});
                    dead_cells.push_back(idx);
                    cell->second.state = Cell::State::DEAD;
                    death_count++;
                    //spdlog::trace("Cell {} died", cells[i].id);
                } 
                else if (birth_probs(i) <= tau) {
                    new_cells_count++;

                    cells_graveyard.insert({cell->first, {cell->second.parent_id, tau}});
                    dead_cells.push_back(idx);
                    cell->second.state = Cell::State::DEAD;
                    death_count++;
                    double rand_val = (Eigen::VectorXd::Random(1)(0) + 1.0) / 2.0; // Random val from 0.0 to 1.0
                    double prob_sum = 0.0;
                            
                    if (rand_val >= total_mutation_probability) {

                        Cell daughter_cell1 = Cell(
                            cell->second,
                            0,
                            cell->second.fitness 
                        );
                        new_cells_count++;

                        Cell daughter_cell2 = Cell(
                            cell->second,
                            0,
                            cell->second.fitness 
                        );
                        //spdlog::trace("Cell {} born without mutation", daughter_cell1.id);
                        //spdlog::trace("Cell {} born without mutation", daughter_cell2.id);
                        new_cells.push_back(std::move(daughter_cell1));
                        new_cells.push_back(std::move(daughter_cell2));
                    }else {
                        double prob_sum = 0.0;
                        for (const auto& mut : available_mutations) {
                            prob_sum += mut.probability;
                            if (rand_val < prob_sum) {
                                
                                Cell daughter_cell1 = Cell(
                                    cell->second,
                                    0,
                                    cell->second.fitness * (1.0 + mut.effect) 
                                );
                                daughter_cell1.mutations.push_back(mut);
                                new_cells_count++;

                                Cell daughter_cell2 = Cell(
                                    cell->second,
                                    0,
                                    cell->second.fitness 
                                );
                                //spdlog::trace("Cell {} born with mutation {}", daughter_cell1.id, toString(mut.type));
                                //spdlog::trace("Cell {} born without mutation", daughter_cell2.id);
                                new_cells.push_back(std::move(daughter_cell1));
                                new_cells.push_back(std::move(daughter_cell2));
                                break;
                            }
                        }
                    }
                }
            }
        }
    });

    if (death_count != dead_cells.size()) {
        spdlog::error("Post Mismatch in death count: expected {}, found {}", death_count, dead_cells.size());
    }

    if (new_cells_count != new_cells.size()) {
        spdlog::error("Post Mismatch in new cells count: expected {}, found {}", new_cells_count, new_cells.size());
    }
   
    auto starting_id = N + total_deaths;
    std::atomic<uint32_t> cells_added(0);
    for (size_t i = 0; i < new_cells.size(); ++i) {
        CellMap::accessor accessor;
        new_cells[i].id = starting_id + i;
        cells.insert(accessor, {starting_id + i, std::move(new_cells[i])});
        cells_added++;
        //spdlog::trace("Cell {} born, Total: {}", new_cells[i].id, cells_added);
    }

    if (cells_added != new_cells_count) {
        spdlog::error("Post Mismatch in new cells count: expected {}, found {}", new_cells_count, cells_added);
    }

    int cells_removed = 0;
    for (const auto& dead_id : dead_cells) {
        cells.erase(dead_id);
        cells_removed++;
        //spdlog::trace("Cell {} removed, Total {}", dead_id, cells_removed);
    }

    if(cells_removed != death_count) {
        spdlog::error("Post Mismatch in dead cells count: expected {}, found {}", death_count, cells_removed);
    }
    
    total_deaths += death_count;
    actual_population += new_cells_count;
    actual_population -= death_count;
    spdlog::info("Step {:.3f} {} -> {} cells | New cells: {} | Dead cells: {} Total deaths: {}", tau,N, actual_population, new_cells_count, death_count, total_deaths);

    if (cells.size() != actual_population) {
        spdlog::error("Post Mismatch in cell count: expected {}, found {}", actual_population, cells.size());
    }
    if (total_deaths != cells_graveyard.size()) {
        spdlog::error("Post Mismatch in graveyard count: expected {}, found {}", total_deaths, cells_graveyard.size());
    }
}

