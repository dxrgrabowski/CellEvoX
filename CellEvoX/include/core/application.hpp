#pragma once
#include <boost/program_options.hpp>
#include <memory>

#include "systems/SimulationEngine.hpp"

namespace po = boost::program_options;
namespace CellEvoX::core {

class Application {
 public:
  Application(po::variables_map& vm);
  ~Application();

  void initialize();
  void update();
  float calculateDeltaTime();

 private:
  po::variables_map& vm;
  std::unique_ptr<SimulationEngine> sim_engine;
  std::shared_ptr<SimulationConfig> sim_config;
  std::vector<std::shared_ptr<ecs::Run>> runs;
};

}  // namespace CellEvoX::core
