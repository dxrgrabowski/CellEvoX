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