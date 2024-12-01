#include "core/RunDataEngine.hpp"
#include <sstream>
#include <cmath>
#include <spdlog/spdlog.h>
#include <map>
#include <external/matplotlibcpp.h>

namespace CellEvoX::core {

namespace plt = matplotlibcpp;

RunDataEngine::RunDataEngine(
    std::shared_ptr<SimulationConfig> config, 
    std::shared_ptr<ecs::Run> run, 
    double generation_step)
    : config(config), run(run), 
    generation_step(generation_step) {}

void RunDataEngine::exportToCharts() 
{

}


// void RunDataEngine::exportToCSV() {

// }

} // namespace core