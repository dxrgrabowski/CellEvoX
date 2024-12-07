#pragma once

#include <tbb/concurrent_hash_map.h>
#include <vector>
#include <random>
#include <spdlog/spdlog.h>
#include "ecs/Cell.hpp"
#include <unordered_set>
#include <vector>

struct StatSnapshot;
namespace ecs {
struct NodeData {
    uint32_t parent_id;
    uint32_t child_sum = 0;
    double death_time;
};
using CellMap = tbb::concurrent_hash_map<uint32_t, Cell>;
using Graveyard = tbb::concurrent_hash_map<uint32_t, std::pair<uint32_t, double>>;
class Run {
public:
    CellMap cells;
    std::unordered_map<uint8_t, MutationType> mutation_id_to_type;
    tbb::concurrent_hash_map<uint32_t, NodeData> phylogenic_tree;
    Graveyard cells_graveyard; 
    std::vector<StatSnapshot> generational_stat_report;
    size_t total_deaths = 0;
    size_t total_mutations = 0;
    int driver_mutations = 0;
    int positive_mutations = 0;
    int neutral_mutations = 0;
    int negative_mutations = 0;
    double average_mutations = 0.0;
    size_t total_cell_memory_usage = 0;
    size_t total_mutations_memory = 0;
    size_t total_graveyard_memory = 0;
    double tau = 0.0;

    Run(
        CellMap &&cells, 
        std::unordered_map<uint8_t, MutationType> mutation_id_to_type, 
        Graveyard &&cells_graveyard,
        std::vector<StatSnapshot> &&generational_stat_report,
        size_t deaths, 
        double tau
    )   : cells(std::move(cells)),
        mutation_id_to_type(std::move(mutation_id_to_type)),
        cells_graveyard(std::move(cells_graveyard)),
        generational_stat_report(std::move(generational_stat_report)),
        total_deaths(deaths),
        tau(tau) 
    {

        processRunInfo();
        logResults();
        checkRunCorrectness();
    }

    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;

    Run(Run&&) = default;
    Run& operator=(Run&&) = default;

    void logResults() const {
        spdlog::info("Simulation ended at time {}", tau);
        spdlog::info("Total cells: {} with:", cells.size());
        spdlog::info("    average mutations per cell: {:.2f}", average_mutations);
        spdlog::info("Total deaths: {}", total_deaths);
        spdlog::info("Total mutations: {} with:", total_mutations);
        spdlog::info("    Driver mutations: {}", driver_mutations);
        spdlog::info("    Positive mutations: {}", positive_mutations);
        spdlog::info("    Neutral mutations: {}", neutral_mutations);
        spdlog::info("    Negative mutations: {}", negative_mutations);
        size_t total_memory_usage = total_cell_memory_usage + total_mutations_memory + total_graveyard_memory;
        spdlog::info("Total memory usage: {} KB with:", total_memory_usage / (1024));
        spdlog::info("   Alive cells memory usage: {} KB", total_cell_memory_usage/ (1024));
        spdlog::info("   Graveyard memory usage: {} KB", total_graveyard_memory / (1024));
        spdlog::info("   Mutations memory usage: {} KB", total_mutations_memory / (1024));
    }

    void processRunInfo()
    {
        int N = cells.size();

       for (const auto& cell : cells) {
            total_mutations += cell.second.mutations.size();
            for (const auto& mutation_id : cell.second.mutations) {
                MutationVariant type = mutation_id_to_type[mutation_id.second].type;
                switch (type) {
                    case MutationVariant::DRIVER:
                        ++driver_mutations;
                        break;
                    case MutationVariant::POSITIVE:
                        ++positive_mutations;
                        break;
                    case MutationVariant::NEUTRAL:
                        ++neutral_mutations;
                        break;
                    case MutationVariant::NEGATIVE:
                        ++negative_mutations;
                        break;
                }
            }
        }

        average_mutations = static_cast<double>(total_mutations) / N;
        total_mutations_memory = N * average_mutations * sizeof(std::pair<uint32_t, uint8_t>);
        total_cell_memory_usage = N * sizeof(Cell);

        total_graveyard_memory = cells_graveyard.size() * sizeof(std::pair<uint32_t, std::pair<uint32_t, double>>);

        
        tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
        if (!phylogenic_tree.find(accessor, 0)) {
            phylogenic_tree.insert({0, {0, 0, 0.0}});
        }

        // child sum counting
        for (const auto& [cell_id, cell_data] : cells) {
            uint32_t current_id = cell_id;

            while (true) {
                NodeData node;

               
                {
                    tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
                    if (phylogenic_tree.find(accessor, current_id)) {
                        node = accessor->second; 
                    } else {
                        Graveyard::accessor g_accessor;
                        CellMap::accessor c_accessor;
                        if (cells_graveyard.find(g_accessor, current_id)) {
                            const auto& [parent_id, death_time] = g_accessor->second;
                            node = {parent_id, 0, death_time};
                        } else if (cells.find(c_accessor, current_id)) {
                            const auto& parent_id = c_accessor->second.parent_id;
                            node = {parent_id, 0, 0.0};
                        } 
                        else {
                            node = {0, 0, 0.0}; 
                            spdlog::error("Cell with ID {} not found in cells or graveyard", current_id);
                        }
                        phylogenic_tree.insert({current_id, node});
                    }
                }

                {
                    tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
                    phylogenic_tree.find(accessor, current_id);
                    accessor->second.child_sum++;
                    current_id = accessor->second.parent_id;
                }

                if (current_id == 0) {
                    break;
                }
            }
        }
        spdlog::debug("Child sum counting finished");
        
        // Add this at the beginning of your function
        auto start_time = std::chrono::high_resolution_clock::now();
        int deleted_nodes_count = 0;
        
        for (auto it = phylogenic_tree.begin(); it != phylogenic_tree.end();) {
            uint32_t current_id = it->first;
            auto& current_node = it->second;
            //spdlog::debug("Checking cell ID {}", current_id);
            // Pomijamy żywe komórki (według progu death_time)
            if (current_node.death_time < 0.0025) { //fix
                ++it;
                continue;
            }
        
            uint32_t parent_id = current_node.parent_id;
            uint32_t child_sum = current_node.child_sum;
        
            bool is_duplicate = false;
        
            // Przejście w górę drzewa w celu sprawdzenia duplikatów
            while (parent_id != 0) {
                tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
                if (phylogenic_tree.find(accessor, parent_id)) {
                    NodeData& parent_node = accessor->second;
        
                    // Jeśli znaleziono martwego rodzica z takim samym child_sum
                    if (parent_node.death_time > 0.0 && parent_node.child_sum == child_sum) {
                        is_duplicate = true;
                        break;
                    }
        
                    // Przejdź do następnego rodzica
                    parent_id = parent_node.parent_id;
                } else {
                    break; // Jeśli rodzic nie istnieje w mapie, przerwij
                }
            }
        
            if (is_duplicate) {
                tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
                if (phylogenic_tree.find(accessor, current_id)) {
                    uint32_t node_parent_id = accessor->second.parent_id;
        
                    // Aktualizacja parent_id dzieci
                    for (auto& [child_id, child_node] : phylogenic_tree) {
                        if (child_node.parent_id == current_id) {
                            child_node.parent_id = node_parent_id;
                        }
                    }
        
                    phylogenic_tree.erase(accessor);
                    ++deleted_nodes_count; // Increment the counter
                }
        
                it = phylogenic_tree.begin(); 
            } else {
                ++it; 
            }
        }
        
        // Add this at the end of your function
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        spdlog::info("Preprocessing step took {} seconds", elapsed.count());
        spdlog::info("Number of deleted nodes: {}", deleted_nodes_count);
    
    }
    // Check duplicate cell IDs and ID consistency
    void checkRunCorrectness() const {

        std::unordered_set<uint64_t> cell_ids;
        uint64_t max_id = 0;

        for (const auto& cell : cells) {
            if (!cell_ids.insert(cell.second.id).second) {
                spdlog::error("Duplicate cell ID found: {}", cell.second.id);
            }
            if (cell.second.id > max_id) {
                max_id = cell.second.id;
            }
        }

        for (const auto& cell : cells_graveyard) {
            if (!cell_ids.insert(cell.first).second) {
                spdlog::error("Duplicate cell ID found in graveyard: {}", cell.first);
            }
            if (cell.first > max_id) {
                max_id = cell.first;
            }
        }

        if (max_id + 1 != cells.size() + cells_graveyard.size()) {
            spdlog::error("Mismatch in cell count and max ID: max ID {}, total cells {}", max_id, cells.size() + cells_graveyard.size());
        } else {
            spdlog::debug("Cell count matches max ID.");
        }

        if (total_deaths != cells_graveyard.size()) {
            spdlog::error("Post Mismatch in graveyard count: expected {}, found {}", total_deaths, cells_graveyard.size());
        }
    }
};

} // namespace ecs