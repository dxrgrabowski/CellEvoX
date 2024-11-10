#pragma once
#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>
#include <Eigen/Dense>

namespace utils {
    class FitnessCalculator {
    public:
        static Eigen::VectorXd getCellsFitnessVector(const tbb::concurrent_vector<Cell, Cell::CellAllocator>& cells) {
            Eigen::VectorXd fitnessVector(cells.size());
            
            tbb::parallel_for(size_t(0), cells.size(), [&](size_t i) {
                fitnessVector(i) = cells[i].fitness;
            });
            
            return fitnessVector;
        }
        
        // Batch version for better performance when processing multiple times
        static void updateFitnessVector(const tbb::concurrent_vector<Cell>& cells, 
                                      Eigen::VectorXd& fitnessVector) {
            fitnessVector.resize(cells.size());
            
            tbb::parallel_for(size_t(0), cells.size(), [&](size_t i) {
                fitnessVector(i) = cells[i].fitness;
            });
        }
    };
}