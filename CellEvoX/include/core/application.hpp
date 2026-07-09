#pragma once
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "systems/SimulationEngine.hpp"

class SimulationEngine3D;
class SimulationEngine3DCapacity;

namespace CellEvoX::core {

enum class PostprocessMode {
  Full,
  Exports,
};

struct CliOptions {
  std::optional<std::string> config_path;
  std::optional<std::string> analyze_path;
  std::optional<std::size_t> max_threads;
  PostprocessMode postprocess_mode = PostprocessMode::Full;
};

class Application {
 public:
  explicit Application(CliOptions options);
  ~Application();

  void initialize();
  void update();
  float calculateDeltaTime();

 private:
  CliOptions options;
  std::unique_ptr<SimulationEngine> sim_engine;
  std::unique_ptr<SimulationEngine3D> sim_engine_3d;
  std::unique_ptr<SimulationEngine3DCapacity> sim_engine_3d_capacity;
  std::shared_ptr<SimulationConfig> sim_config;
  std::vector<std::shared_ptr<ecs::Run>> runs;
};

}  // namespace CellEvoX::core
