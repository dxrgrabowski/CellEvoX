#pragma once

#include <tbb/concurrent_hash_map.h>
#include <vector>
#include <random>
#include <spdlog/spdlog.h>
#include "ecs/Cell.hpp"
#include <unordered_set>
namespace ecs {

using CellMap = tbb::concurrent_hash_map<uint32_t, Cell>;
class Run {
public:
    CellMap cells;
    size_t total_deaths = 0;
    size_t total_mutations = 0;
    int driver_mutations = 0;
    int positive_mutations = 0;
    int neutral_mutations = 0;
    int negative_mutations = 0;
    double average_mutations = 0.0;
    size_t total_memory_usage = 0;
    double tau = 0.0;

    Run(CellMap cells, size_t deaths, double tau)
        : cells(std::move(cells)),
          total_deaths(deaths),
          tau(tau) {
        processRunInfo();
        logResults();
        checkRunCorrectness();
    }

    Run(const Run&) = delete; // Prevent copying
    Run& operator=(const Run&) = delete;

    Run(Run&&) = default; // Allow move semantics
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
        spdlog::info("Total memory usage: {} MB with:", total_memory_usage / (1024 * 1024));
        
        int total_cell_memory = cells.size() * sizeof(Cell) / (1024 * 1024);
        spdlog::info("    Cells memory usage: {} MB", total_cell_memory);
        
        size_t total_mutations_memory = 0;
        size_t mutation_per_cell_mem = average_mutations * sizeof(Mutation);
        size_t total_mutation_memory = mutation_per_cell_mem * cells.size();
        spdlog::info("    Mutations memory usage: {} MB", total_mutation_memory / (1024 * 1024));
    }

    void processRunInfo()
    {
        int N = cells.size();

        for (const auto& cell : cells) {
            total_mutations += cell.second.mutations.size();
            for (const auto& mutation : cell.second.mutations) {
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

        average_mutations = static_cast<double>(total_mutations) / N;
        size_t mutation_vector_size = average_mutations * sizeof(Mutation);
        total_memory_usage = mutation_vector_size * N + N * sizeof(Cell);
    }

    void checkRunCorrectness() const {

        // Check duplicate cell IDs and ID consistency
        std::unordered_set<uint64_t> cell_ids;
        uint64_t max_id = 0;

        for (const auto& cell : cells) {
            if (!cell_ids.insert(cell.second.id).second) {
                spdlog::error("Duplicate cell ID found: {}", cell.second.id);
            }
        }
    }
};

} // namespace ecs