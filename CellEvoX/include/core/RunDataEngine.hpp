#pragma once

#include "ecs/Run.hpp"
#include <tbb/concurrent_vector.h>
#include <string>
#include <fstream>

namespace CellEvoX::core {

class RunDataEngine {
public:
    RunDataEngine(double generation_step = 0.005);

    // Generate a graph in Graphviz DOT format
    void generateGraphvizDot(const tbb::concurrent_vector<Cell>& cells, const std::string& output_path);
    void exportToGEXF(const tbb::concurrent_vector<Cell>& cells, const std::string& output_file, const double& sim_end);
    void exportToCSV(const tbb::concurrent_vector<Cell>& cells, const std::string& output_file);
private:
    double generation_step; // Time step for separating generations

    // Helper to classify cells into layers by generation
    std::vector<std::vector<const Cell*>> classifyByGeneration(const tbb::concurrent_vector<Cell>& cells);
};

} // namespace core