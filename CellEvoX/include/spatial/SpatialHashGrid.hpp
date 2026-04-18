#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

class SpatialHashGrid {
 public:
  explicit SpatialHashGrid(float voxel_size, float domain_size);

  // O(N log N) rebuild based on voxel-hash sorting.
  void rebuild(const std::vector<uint32_t>& ids,
               const std::vector<float>& px,
               const std::vector<float>& py,
               const std::vector<float>& pz);

  // O(1) per query for fixed r / voxel_size.
  template <typename Callback>
  void queryRadius(float x, float y, float z, float r, Callback&& cb) const {
    if (sorted_ids_.empty()) {
      return;
    }

    const int center_ix = clampVoxelIndex(static_cast<int>(std::floor(x / voxel_size_)));
    const int center_iy = clampVoxelIndex(static_cast<int>(std::floor(y / voxel_size_)));
    const int center_iz = clampVoxelIndex(static_cast<int>(std::floor(z / voxel_size_)));
    const int span = static_cast<int>(std::ceil(r / voxel_size_));

    const int ix_min = std::max(0, center_ix - span);
    const int iy_min = std::max(0, center_iy - span);
    const int iz_min = std::max(0, center_iz - span);
    const int ix_max = std::min(grid_dim_ - 1, center_ix + span);
    const int iy_max = std::min(grid_dim_ - 1, center_iy + span);
    const int iz_max = std::min(grid_dim_ - 1, center_iz + span);

    for (int iz = iz_min; iz <= iz_max; ++iz) {
      for (int iy = iy_min; iy <= iy_max; ++iy) {
        for (int ix = ix_min; ix <= ix_max; ++ix) {
          const int64_t hash = hashVoxel(ix, iy, iz);
          const auto range_it = voxel_ranges_.find(hash);
          if (range_it == voxel_ranges_.end()) {
            continue;
          }

          const auto [begin_idx, end_idx] = range_it->second;
          for (int32_t idx = begin_idx; idx < end_idx; ++idx) {
            cb(sorted_ids_[static_cast<size_t>(idx)]);
          }
        }
      }
    }
  }

  int64_t hashVoxel(int ix, int iy, int iz) const;

 private:
  int clampVoxelIndex(int value) const;

  float voxel_size_;
  int grid_dim_;

  std::vector<uint32_t> sorted_ids_;
  std::vector<int32_t> cell_voxel_;
  std::unordered_map<int64_t, std::pair<int32_t, int32_t>> voxel_ranges_;
};
