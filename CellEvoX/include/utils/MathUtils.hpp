#pragma once
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>

#include <Eigen/Dense>

namespace utils {
class FitnessCalculator {
 public:
  static Eigen::VectorXd getCellsFitnessVector(const CellMap& cells,
                                               const std::vector<uint32_t>& alive_indices) {
    Eigen::VectorXd fitnessVector(alive_indices.size());

    tbb::parallel_for(uint32_t(0), (uint32_t)alive_indices.size(), [&](uint32_t idx) {
      CellMap::const_accessor accessor;
      if (cells.find(accessor, alive_indices[idx])) {
        fitnessVector(idx) = accessor->second.fitness;
      }
    });

    return fitnessVector;
  }

  // Batch version for better performance when processing multiple times
  static void updateFitnessVector(const tbb::concurrent_vector<Cell>& cells,
                                  Eigen::VectorXd& fitnessVector) {
    fitnessVector.resize(cells.size());

    tbb::parallel_for(
        size_t(0), cells.size(), [&](size_t i) { fitnessVector(i) = cells[i].fitness; });
  }
};
}  // namespace utils