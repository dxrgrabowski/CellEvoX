#include "ecs/Run.hpp"

#include "systems/SimulationEngine.hpp"
namespace ecs {

Run::Run(CellMap&& cells,
         std::unordered_map<uint8_t, MutationType> mutation_id_to_type,
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
  processRunInfo();
  logResults();
  checkRunCorrectness();
  createPhylogeneticTree();
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
  tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
  if (!phylogenetic_tree.find(accessor, 0)) {
    phylogenetic_tree.insert({0, {0, 0, 0.0}});
    spdlog::debug("Root node 0 inserted");
  }

  // child sum counting
  for (const auto& [cell_id, cell_data] : cells) {
    uint32_t current_id = cell_id;

    while (true) {
      NodeData node;
      {
        tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
        if (phylogenetic_tree.find(accessor, current_id)) {
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
          } else {
            node = {0, 0, 0.0};
            spdlog::error("Cell with ID {} not found in cells or graveyard", current_id);
          }
          phylogenetic_tree.insert({current_id, node});
        }
      }

      {
        tbb::concurrent_hash_map<uint32_t, NodeData>::accessor accessor;
        phylogenetic_tree.find(accessor, current_id);
        accessor->second.child_sum++;
        current_id = accessor->second.parent_id;
      }

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
  std::unordered_set<uint32_t> visited_nodes;
  std::vector<uint32_t> nodes_to_be_removed;
  for (const auto& [cell_id, cell_data] : cells) {
    uint32_t current_id = cell_id;

    while (current_id != 0) {
      if (visited_nodes.count(current_id)) {
        break;
      }

      visited_nodes.insert(current_id);

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

      tbb::concurrent_hash_map<uint32_t, NodeData>::accessor parent_accessor;
      if (!phylogenetic_tree.find(parent_accessor, parent_id)) {
        spdlog::error("Parent node with ID {} not found", parent_id);
        break;
      }
      NodeData& parent_node = parent_accessor->second;

      if (current_node.child_sum == parent_node.child_sum) {
        uint32_t next_parent_id = parent_node.parent_id;
        nodes_to_be_removed.push_back(parent_id);

        while (next_parent_id != 0) {
          tbb::concurrent_hash_map<uint32_t, NodeData>::accessor next_parent_accessor;
          if (!phylogenetic_tree.find(next_parent_accessor, next_parent_id)) {
            spdlog::error("Parent node with ID {} not found", next_parent_id);
            break;
          }
          NodeData& next_parent_node = next_parent_accessor->second;

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

  total_graveyard_memory =
      cells_graveyard.size() * sizeof(std::pair<uint32_t, std::pair<uint32_t, double>>);
}
// Check duplicate cell IDs and ID consistency
void Run::checkRunCorrectness() const {
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
    spdlog::error("Mismatch in cell count and max ID: max ID {}, total cells {}",
                  max_id,
                  cells.size() + cells_graveyard.size());
  } else {
    spdlog::debug("Cell count matches max ID.");
  }

  if (total_deaths != cells_graveyard.size()) {
    spdlog::error("Post Mismatch in graveyard count: expected {}, found {}",
                  total_deaths,
                  cells_graveyard.size());
  }
}

}  // namespace ecs