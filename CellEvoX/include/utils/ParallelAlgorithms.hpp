#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>

#include <tbb/parallel_sort.h>

namespace CellEvoX::parallel_algorithms {

inline constexpr std::ptrdiff_t kParallelSortMinItems = 2048;

template <typename RandomIt, typename Compare>
void sortMaybeParallel(RandomIt first, RandomIt last, Compare compare) {
  const auto count = std::distance(first, last);
  if (count < kParallelSortMinItems) {
    std::sort(first, last, compare);
    return;
  }

  tbb::parallel_sort(first, last, compare);
}

template <typename RandomIt>
void sortMaybeParallel(RandomIt first, RandomIt last) {
  sortMaybeParallel(first, last, std::less<>{});
}

}  // namespace CellEvoX::parallel_algorithms
