#include "spatial/SpatialHashGrid.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include "utils/ParallelAlgorithms.hpp"

namespace {

struct HashedCellEntry {
  int64_t voxel_hash;
  uint32_t cell_id;
};

constexpr int64_t kMaxDenseVoxelRanges = 1'048'576;

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
  dense_voxel_ranges_.clear();
  voxel_ranges_.clear();
  use_dense_ranges_ = false;

  if (count == 0) {
    return;
  }

  if (px.size() != count || py.size() != count || pz.size() != count) {
    throw std::invalid_argument("SpatialHashGrid::rebuild received mismatched array sizes");
  }

  const int64_t dense_voxel_count = static_cast<int64_t>(grid_dim_) * grid_dim_ * grid_dim_;
  if (dense_voxel_count > 0 && dense_voxel_count <= kMaxDenseVoxelRanges) {
    const size_t voxel_count = static_cast<size_t>(dense_voxel_count);
    use_dense_ranges_ = true;
    sorted_ids_.resize(count);
    cell_voxel_.resize(count);
    dense_voxel_ranges_.assign(voxel_count, {0, 0});

    tbb::enumerable_thread_specific<std::vector<int32_t>> local_counts(
        [voxel_count] { return std::vector<int32_t>(voxel_count, 0); });

    tbb::parallel_for(tbb::blocked_range<size_t>(0, count), [&](const tbb::blocked_range<size_t>& range) {
      auto& counts = local_counts.local();
      for (size_t i = range.begin(); i != range.end(); ++i) {
        const int ix = clampVoxelIndex(static_cast<int>(std::floor(px[i] / voxel_size_)));
        const int iy = clampVoxelIndex(static_cast<int>(std::floor(py[i] / voxel_size_)));
        const int iz = clampVoxelIndex(static_cast<int>(std::floor(pz[i] / voxel_size_)));
        const auto voxel = static_cast<int32_t>(hashVoxel(ix, iy, iz));
        cell_voxel_[i] = voxel;
        ++counts[static_cast<size_t>(voxel)];
      }
    });

    std::vector<int32_t> voxel_counts(voxel_count, 0);
    for (const auto& counts : local_counts) {
      for (size_t voxel = 0; voxel < voxel_count; ++voxel) {
        voxel_counts[voxel] += counts[voxel];
      }
    }

    int32_t offset = 0;
    for (size_t voxel = 0; voxel < voxel_count; ++voxel) {
      const int32_t begin = offset;
      offset += voxel_counts[voxel];
      dense_voxel_ranges_[voxel] = {begin, offset};
      voxel_counts[voxel] = begin;
    }

    for (size_t i = 0; i < count; ++i) {
      const auto voxel = static_cast<size_t>(cell_voxel_[i]);
      sorted_ids_[static_cast<size_t>(voxel_counts[voxel]++)] = ids[i];
    }
    return;
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

  CellEvoX::parallel_algorithms::sortMaybeParallel(
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

  tbb::parallel_for(tbb::blocked_range<size_t>(0, count), [&](const tbb::blocked_range<size_t>& range) {
    for (size_t i = range.begin(); i != range.end(); ++i) {
      sorted_ids_[i] = hashed_cells[i].cell_id;
      cell_voxel_[i] = static_cast<int32_t>(hashed_cells[i].voxel_hash);
    }
  });

  int64_t current_hash = hashed_cells.front().voxel_hash;
  int32_t range_begin = 0;

  // O(N) linear scan over sorted voxel buckets.
  for (size_t i = 0; i < count; ++i) {
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
