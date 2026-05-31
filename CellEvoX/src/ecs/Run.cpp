#include "ecs/Run.hpp"

#include "systems/SimulationEngine.hpp"
#include "utils/PhaseProfiler.hpp"
namespace ecs {

Run::Run(CellMap&& cells,
         std::map<uint8_t, MutationType> mutation_id_to_type,
         Graveyard&& cells_graveyard,
         std::vector<StatSnapshot>&& generational_stat_report,
         std::vector<std::pair<int, CellMap>> generational_popul_report,
         size_t deaths,
         double tau)
    : cells(std::move(cells)),
      mutation_id_to_type(std::move(mutation_id_to_type)),
      cells_graveyard(std::move(cells_graveyard)),
      generational_stat_report(std::move(generational_stat_report)),
      generational_popul_report(std::move(generational_popul_report)),
      total_deaths(deaths),
      tau(tau) {
  CELLEVOX_PROFILE_PHASE("run_postprocessing");
  {
    CELLEVOX_PROFILE_PHASE("run_process_info");
    processRunInfo();
  }
  {
    CELLEVOX_PROFILE_PHASE("run_log_results");
    logResults();
  }
  {
    CELLEVOX_PROFILE_PHASE("run_correctness_check");
    checkRunCorrectness();
  }
  {
    CELLEVOX_PROFILE_PHASE("run_create_phylogenetic_tree");
    createPhylogeneticTree();
  }
}

void Run::logResults() const {
  spdlog::info("Simulation ended at generation {}", tau);
  spdlog::info("Total cells: {} with:", cells.size());
  spdlog::info("    average mutations per cell: {:.2f}", average_mutations);
  spdlog::info("Total deaths: {}", total_deaths);
  spdlog::info("Total mutations: {} with:", total_mutations);
  spdlog::info("    Driver mutations: {}", driver_mutations);
  spdlog::info("    Positive mutations: {}", positive_mutations);
  spdlog::info("    Neutral mutations: {}", neutral_mutations);
  spdlog::info("    Negative mutations: {}", negative_mutations);
  size_t total_memory_usage =
      total_cell_memory_usage + total_mutations_memory + total_graveyard_memory;
  spdlog::info("Total memory usage: {} KB with:", total_memory_usage / (1024));
  spdlog::info("   Alive cells memory usage: {} KB", total_cell_memory_usage / (1024));
  spdlog::info("   Graveyard memory usage: {} KB", total_graveyard_memory / (1024));
  spdlog::info("   Mutations memory usage: {} KB", total_mutations_memory / (1024));
}
void Run::createPhylogeneticTree() {
  {
    tbb::concurrent_hash_map<uint32_t, NodeData>::accessor root_accessor;
    if (phylogenetic_tree.insert(root_accessor, 0)) {
      root_accessor->second = {0, 0, 0.0};
      spdlog::debug("Root node 0 inserted");
    }
  }

  // child sum counting
  for (const auto& [cell_id, cell_data] : cells) {
    uint32_t current_id = cell_id;

    while (true) {
      uint32_t parent_id = 0;
      {
        tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
        if (phylogenetic_tree.insert(accessor, current_id)) {
          if (current_id == cell_id) {
            accessor->second = {cell_data.parent_id, 0, 0.0};
          } else {
            Graveyard::const_accessor g_accessor;
            CellMap::const_accessor c_accessor;
            if (cells_graveyard.find(g_accessor, current_id)) {
              const auto& [graveyard_parent_id, death_time] = g_accessor->second;
              accessor->second = {graveyard_parent_id, 0, death_time};
            } else if (cells.find(c_accessor, current_id)) {
              const auto& cell_parent_id = c_accessor->second.parent_id;
              accessor->second = {cell_parent_id, 0, 0.0};
            } else {
              accessor->second = {0, 0, 0.0};
              spdlog::error("Cell with ID {} not found in cells or graveyard", current_id);
            }
          }
        }

        accessor->second.child_sum++;
        parent_id = accessor->second.parent_id;
      }
      current_id = parent_id;

      if (current_id == 0) {
        tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
        phylogenetic_tree.find(accessor, current_id);
        accessor->second.child_sum++;
        break;
      }
    }
  }
  spdlog::debug("Child sum counting finished");

  auto start_time = std::chrono::high_resolution_clock::now();
  int deleted_nodes_count = 0;
  const size_t lineage_count = cells.size() + cells_graveyard.size() + 1;
  std::unordered_set<uint32_t> visited_nodes;
  visited_nodes.reserve(lineage_count);
  std::vector<uint32_t> nodes_to_be_removed;
  nodes_to_be_removed.reserve(cells_graveyard.size());
  for (const auto& [cell_id, cell_data] : cells) {
    uint32_t current_id = cell_id;

    while (current_id != 0) {
      if (!visited_nodes.insert(current_id).second) {
        break;
      }

      tbb::concurrent_hash_map<uint32_t, NodeData>::accessor current_accessor;
      if (!phylogenetic_tree.find(current_accessor, current_id)) {
        spdlog::error("Current node with ID {} not found", current_id);
        break;
      }
      NodeData& current_node = current_accessor->second;

      uint32_t parent_id = current_node.parent_id;
      if (parent_id == 0) {
        break;
      }

      tbb::concurrent_hash_map<uint32_t, NodeData>::const_accessor parent_accessor;
      if (!phylogenetic_tree.find(parent_accessor, parent_id)) {
        spdlog::error("Parent node with ID {} not found", parent_id);
        break;
      }
      const NodeData& parent_node = parent_accessor->second;

      if (current_node.child_sum == parent_node.child_sum) {
        uint32_t next_parent_id = parent_node.parent_id;
        nodes_to_be_removed.push_back(parent_id);

        while (next_parent_id != 0) {
          tbb::concurrent_hash_map<uint32_t, NodeData>::const_accessor next_parent_accessor;
          if (!phylogenetic_tree.find(next_parent_accessor, next_parent_id)) {
            spdlog::error("Parent node with ID {} not found", next_parent_id);
            break;
          }
          const NodeData& next_parent_node = next_parent_accessor->second;

          if (next_parent_node.child_sum > current_node.child_sum) {
            current_node.parent_id = next_parent_id;
            break;
          } else {
            nodes_to_be_removed.push_back(next_parent_id);
          }

          next_parent_id = next_parent_node.parent_id;
        }

        if (next_parent_id == 0) {
          current_node.parent_id = 0;
        }
      }

      current_id = current_node.parent_id;
    }
  }
  for (uint32_t node_id : nodes_to_be_removed) {
    tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
    if (phylogenetic_tree.find(accessor, node_id)) {
      phylogenetic_tree.erase(accessor);
      ++deleted_nodes_count;
    }
  }
  // Add this at the end of your function
  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end_time - start_time;
  spdlog::info("Phylogenetic tree postprocessing took {} seconds", elapsed.count());
  spdlog::info("    Number of deleted nodes: {}", deleted_nodes_count);
}

void Run::processRunInfo() {
  int N = cells.size();

  for (const auto& cell : cells) {
    total_mutations += cell.second.mutations.size();
    for (const auto& mutation_id : cell.second.mutations) {
      const auto& mut_type = mutation_id_to_type[mutation_id.second];
      if (mut_type.is_driver) {
        ++driver_mutations;
      }

      if (mut_type.effect > 0) {
        ++positive_mutations;
      } else if (mut_type.effect < 0) {
        ++negative_mutations;
      } else {
        ++neutral_mutations;
      }
    }
  }

  average_mutations = static_cast<double>(total_mutations) / N;
  total_mutations_memory = N * average_mutations * sizeof(std::pair<uint32_t, uint8_t>);
  total_cell_memory_usage = N * sizeof(Cell);

  total_graveyard_memory =
      cells_graveyard.size() * sizeof(std::pair<uint32_t, std::pair<uint32_t, double>>);
}
// Check duplicate cell IDs and ID consistency
void Run::checkRunCorrectness() const {
  std::unordered_set<uint64_t> cell_ids;
  cell_ids.reserve(cells.size() + cells_graveyard.size());
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
}

}  // namespace ecs
