#pragma once

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "ecs/Run.hpp"
#include "utils/PhaseProfiler.hpp"

namespace CellEvoX::systems {

// Internal population storage shared by both drivers. Ids are monotonic; slots
// are reused after death. Alive ids stay ordered but may contain stale entries.
struct DensePopulation {
  explicit DensePopulation(size_t initial_population = 0) {
    cells.reserve(initial_population);
    alive_cell_ids.reserve(initial_population);
    cell_slot_by_id.reserve(initial_population);
    alive_flags.reserve(initial_population);
    for (uint32_t id = 0; id < initial_population; ++id) {
      cells.emplace_back(id);
      alive_cell_ids.push_back(id);
      cell_slot_by_id.push_back(id);
      alive_flags.push_back(1);
    }
  }

  bool isAlive(uint32_t id) const { return id < alive_flags.size() && alive_flags[id] != 0; }

  const Cell& at(uint32_t id) const { return cells[cell_slot_by_id[id]]; }
  Cell& at(uint32_t id) { return cells[cell_slot_by_id[id]]; }

  void materializeCells(ecs::CellMap& output, size_t actual_population) const {
    CELLEVOX_PROFILE_PHASE("materialize_cells_from_dense");
    output.clear();
    output.rehash(actual_population);
    for (uint32_t id : alive_cell_ids) {
      if (isAlive(id)) {
        output.insert({id, at(id)});
      }
    }
  }

  void materializeGraveyard(ecs::Graveyard& output) {
    if (pending_graveyard_entries.empty()) {
      return;
    }
    CELLEVOX_PROFILE_PHASE("materialize_graveyard_from_dense");
    output.rehash(output.size() + pending_graveyard_entries.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, pending_graveyard_entries.size()),
                      [&](const tbb::blocked_range<size_t>& range) {
                        for (size_t i = range.begin(); i != range.end(); ++i) {
                          output.insert(pending_graveyard_entries[i]);
                        }
                      });
    pending_graveyard_entries.clear();
  }

  std::vector<Cell> cells;
  std::vector<uint32_t> alive_cell_ids;
  std::vector<uint32_t> cell_slot_by_id;
  std::vector<uint8_t> alive_flags;
  std::vector<uint32_t> free_slots;
  std::vector<std::pair<uint32_t, std::pair<uint32_t, double>>> pending_graveyard_entries;
};

}  // namespace CellEvoX::systems
