#include "spatial/SpatialHashGrid.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace {

struct HashedCellEntry {
  int64_t voxel_hash;
  uint32_t cell_id;
};

}  // namespace

SpatialHashGrid::SpatialHashGrid(float voxel_size, float domain_size)
    : voxel_size_(voxel_size),
      grid_dim_(std::max(1, static_cast<int>(std::ceil(domain_size / voxel_size)))) {
  if (voxel_size_ <= 0.0f) {
    throw std::invalid_argument("SpatialHashGrid voxel_size must be positive");
  }
}

void SpatialHashGrid::rebuild(const std::vector<uint32_t>& ids,
                              const std::vector<float>& px,
                              const std::vector<float>& py,
                              const std::vector<float>& pz) {
  const size_t count = ids.size();
  sorted_ids_.clear();
  cell_voxel_.clear();
  voxel_ranges_.clear();

  if (count == 0) {
    return;
  }

  if (px.size() != count || py.size() != count || pz.size() != count) {
    throw std::invalid_argument("SpatialHashGrid::rebuild received mismatched array sizes");
  }

  std::vector<HashedCellEntry> hashed_cells(count);

  // O(N) hash computation over active cells.
  tbb::parallel_for(tbb::blocked_range<size_t>(0, count), [&](const tbb::blocked_range<size_t>& range) {
    for (size_t i = range.begin(); i != range.end(); ++i) {
      const int ix = clampVoxelIndex(static_cast<int>(std::floor(px[i] / voxel_size_)));
      const int iy = clampVoxelIndex(static_cast<int>(std::floor(py[i] / voxel_size_)));
      const int iz = clampVoxelIndex(static_cast<int>(std::floor(pz[i] / voxel_size_)));
      hashed_cells[i] = {hashVoxel(ix, iy, iz), ids[i]};
    }
  });

  std::sort(
      hashed_cells.begin(),
      hashed_cells.end(),
      [](const HashedCellEntry& lhs, const HashedCellEntry& rhs) {
        if (lhs.voxel_hash != rhs.voxel_hash) {
          return lhs.voxel_hash < rhs.voxel_hash;
        }
        return lhs.cell_id < rhs.cell_id;
      });

  sorted_ids_.resize(count);
  cell_voxel_.resize(count);
  voxel_ranges_.reserve(count);

  int64_t current_hash = hashed_cells.front().voxel_hash;
  int32_t range_begin = 0;

  // O(N) linear scan over sorted voxel buckets.
  for (size_t i = 0; i < count; ++i) {
    sorted_ids_[i] = hashed_cells[i].cell_id;
    cell_voxel_[i] = static_cast<int32_t>(hashed_cells[i].voxel_hash);

    if (hashed_cells[i].voxel_hash != current_hash) {
      voxel_ranges_[current_hash] = {range_begin, static_cast<int32_t>(i)};
      current_hash = hashed_cells[i].voxel_hash;
      range_begin = static_cast<int32_t>(i);
    }
  }

  voxel_ranges_[current_hash] = {range_begin, static_cast<int32_t>(count)};
}

int64_t SpatialHashGrid::hashVoxel(int ix, int iy, int iz) const {
  const int64_t dim = static_cast<int64_t>(grid_dim_);
  return static_cast<int64_t>(ix) + static_cast<int64_t>(iy) * dim +
         static_cast<int64_t>(iz) * dim * dim;
}

int SpatialHashGrid::clampVoxelIndex(int value) const {
  return std::clamp(value, 0, grid_dim_ - 1);
}
