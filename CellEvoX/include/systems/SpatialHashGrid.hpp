#pragma once
#include <Eigen/Dense>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ecs/Cell.hpp"

/// Lock-free Uniform Spatial Hash Grid for O(1) amortized neighbor queries.
///
/// Rebuilt from scratch each mechanical sub-step.  Uses parallel_sort + prefix
/// scan to create a flat, cache-friendly structure with zero mutexes.
///
/// Voxel side length L = 2 * CELL_RADIUS so any two overlapping cells
/// (distance < 2R) are guaranteed to be in the same or adjacent voxels.
///
/// Build complexity:  O(N log N)  (dominated by parallel_sort)
/// Query complexity:  O(1) amortized per cell (iterate 27 adjacent voxels)
class SpatialHashGrid {
 public:
  /// Voxel side length: 2 * CELL_RADIUS ensures overlapping cells share
  /// adjacent voxels.
  static constexpr float VOXEL_SIZE = 2.0f * Cell::CELL_RADIUS;
  static constexpr float INV_VOXEL_SIZE = 1.0f / VOXEL_SIZE;

  SpatialHashGrid() = default;

  /// Rebuild the grid from scratch.  Called once per mechanical sub-step.
  ///
  /// @param positions   Contiguous array of cell positions (indexed by active
  ///                    cell order, NOT by cell ID).
  /// @param num_cells   Number of active cells.
  ///
  /// Complexity: O(N log N) due to parallel_sort.
  void rebuild(const Eigen::Vector3f* positions, uint32_t num_cells) {
    if (num_cells == 0) {
      sorted_indices_.clear();
      hashes_.clear();
      voxel_start_.clear();
      voxel_end_.clear();
      return;
    }

    sorted_indices_.resize(num_cells);
    hashes_.resize(num_cells);

    // Step 1: Compute voxel hash for each cell.  O(N), fully parallel.
    tbb::parallel_for(
        tbb::blocked_range<uint32_t>(0, num_cells),
        [&](const tbb::blocked_range<uint32_t>& range) {
          for (uint32_t i = range.begin(); i != range.end(); ++i) {
            hashes_[i] = hashPosition(positions[i]);
            sorted_indices_[i] = i;
          }
        });

    // Step 2: Sort indices by voxel hash.  O(N log N), parallel.
    tbb::parallel_sort(sorted_indices_.begin(), sorted_indices_.end(),
                       [this](uint32_t a, uint32_t b) {
                         return hashes_[a] < hashes_[b];
                       });

    // Step 3: Single-pass prefix scan to record voxel boundaries.  O(N).
    voxel_start_.clear();
    voxel_end_.clear();
    // Reserve capacity to reduce rehashing (expect ~N/8 occupied voxels)
    voxel_start_.reserve(num_cells / 4);
    voxel_end_.reserve(num_cells / 4);

    uint32_t prev_hash = hashes_[sorted_indices_[0]];
    voxel_start_[prev_hash] = 0;

    for (uint32_t i = 1; i < num_cells; ++i) {
      uint32_t cur_hash = hashes_[sorted_indices_[i]];
      if (cur_hash != prev_hash) {
        voxel_end_[prev_hash] = i;
        voxel_start_[cur_hash] = i;
        prev_hash = cur_hash;
      }
    }
    voxel_end_[prev_hash] = num_cells;
  }

  /// Invoke `callback(uint32_t neighbor_index)` for every cell in the 27
  /// voxels surrounding the given position (including the cell's own voxel).
  ///
  /// @param pos        Query position.
  /// @param callback   Callable receiving the *contiguous-array index* of each
  ///                   neighbor (NOT the cell ID).
  ///
  /// Complexity: O(1) amortized (constant number of voxels × cells/voxel).
  template <typename Callback>
  void forEachNeighbor(const Eigen::Vector3f& pos, Callback&& callback) const {
    int32_t cx = voxelCoord(pos.x());
    int32_t cy = voxelCoord(pos.y());
    int32_t cz = voxelCoord(pos.z());

    for (int32_t dx = -1; dx <= 1; ++dx) {
      for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dz = -1; dz <= 1; ++dz) {
          uint32_t h = hashCoords(cx + dx, cy + dy, cz + dz);
          auto it_start = voxel_start_.find(h);
          if (it_start == voxel_start_.end()) continue;
          uint32_t begin = it_start->second;
          uint32_t end = voxel_end_.find(h)->second;
          for (uint32_t k = begin; k < end; ++k) {
            callback(sorted_indices_[k]);
          }
        }
      }
    }
  }

  /// Count cells within a given radius of a position.
  /// Useful for local density estimation.
  ///
  /// @param pos           Query position.
  /// @param radius        Search radius.
  /// @param positions     Array of all cell positions.
  /// @param exclude_self  Index to exclude from the count (the cell itself).
  /// @return Number of cells within radius (excluding self).
  uint32_t countNeighborsInRadius(const Eigen::Vector3f& pos,
                                  float radius,
                                  const Eigen::Vector3f* positions,
                                  uint32_t exclude_self) const {
    float radius_sq = radius * radius;
    uint32_t count = 0;
    forEachNeighbor(pos, [&](uint32_t idx) {
      if (idx == exclude_self) return;
      float dist_sq = (positions[idx] - pos).squaredNorm();
      if (dist_sq <= radius_sq) {
        ++count;
      }
    });
    return count;
  }

 private:
  /// Convert a world-space coordinate to a voxel integer coordinate.
  static int32_t voxelCoord(float x) {
    return static_cast<int32_t>(std::floor(x * INV_VOXEL_SIZE));
  }

  /// Spatial hash from integer voxel coordinates.  Uses large primes to
  /// distribute entries uniformly across the hash table.
  static uint32_t hashCoords(int32_t ix, int32_t iy, int32_t iz) {
    // Large primes for spatial hashing (Teschner et al. 2003)
    constexpr uint32_t p1 = 73856093u;
    constexpr uint32_t p2 = 19349663u;
    constexpr uint32_t p3 = 83492791u;
    return static_cast<uint32_t>(ix) * p1 ^
           static_cast<uint32_t>(iy) * p2 ^
           static_cast<uint32_t>(iz) * p3;
  }

  /// Compute the hash for a world-space position.
  static uint32_t hashPosition(const Eigen::Vector3f& pos) {
    return hashCoords(voxelCoord(pos.x()), voxelCoord(pos.y()),
                      voxelCoord(pos.z()));
  }

  // --- Storage (all flat, contiguous) ---

  /// Cell indices sorted by their voxel hash.
  std::vector<uint32_t> sorted_indices_;

  /// Voxel hash for each cell (indexed by original contiguous order).
  std::vector<uint32_t> hashes_;

  /// Maps voxel hash → index of first cell in sorted_indices_.
  std::unordered_map<uint32_t, uint32_t> voxel_start_;

  /// Maps voxel hash → past-the-end index in sorted_indices_.
  std::unordered_map<uint32_t, uint32_t> voxel_end_;
};
