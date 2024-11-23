#include "core/RunDataEngine.hpp"
#include <sstream>
#include <cmath>
#include <spdlog/spdlog.h>
#include <map>

namespace CellEvoX::core {

RunDataEngine::RunDataEngine(double generation_step)
    : generation_step(generation_step) {}



void RunDataEngine::generateGraphvizDot(const tbb::concurrent_vector<Cell>& cells, const std::string& output_path) {

}

void RunDataEngine::exportToGEXF(const tbb::concurrent_vector<Cell>& cells, const std::string& output_file, const double& sim_end) {
 
}

void RunDataEngine::exportToCSV(const tbb::concurrent_vector<Cell>& cells, const std::string& output_file) {

}

} // namespace core