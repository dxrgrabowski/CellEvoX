#include "core/application.hpp"

#include <spdlog/spdlog.h>

#include <fstream>
#include <csignal>
#include <nlohmann/json.hpp>

#include "core/RunDataEngine.hpp"
#include "ecs/Run.hpp"
#include "systems/SimulationEngine.hpp"
#include "utils/SimulationConfig.hpp"
// #include "core/DatabaseManager.hpp"
namespace CellEvoX::core {

float calculateDeltaTime();

Application::Application(po::variables_map& vm) : vm(vm) { initialize(); }

Application::~Application() = default;

void Application::initialize() {
  spdlog::info("CellEvoX Application starting...");
  spdlog::set_level(spdlog::level::trace);
  // Initialize the simulation engine
  if (vm.count("config")) {
    std::ifstream config_file(vm["config"].as<std::string>());
    nlohmann::json config;
    config_file >> config;
    sim_config = std::make_shared<SimulationConfig>(utils::fromJson(config));
    utils::printConfig(*sim_config);
    sim_engine = std::make_unique<SimulationEngine>(sim_config);
    
    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, SimulationEngine::signalHandler);
    std::signal(SIGTERM, SimulationEngine::signalHandler);
    
    runs.push_back(std::make_shared<ecs::Run>(sim_engine->run(config.at("steps"))));

    std::string config_path = vm["config"].as<std::string>();
    RunDataEngine data_engine(sim_config, runs[0], config_path, 0.005);
    data_engine.plotFitnessStatistics();
    data_engine.plotMutationsStatistics();
    data_engine.plotLivingCellsOverGenerations();
    data_engine.plotMutationWave();
    data_engine.plotMutationFrequency();
    data_engine.exportToCSV();
    data_engine.exportPhylogeneticTreeToGEXF("phylogenetic.gexf");
  }

  // Initialize the database manager
  // DatabaseManager db;

  spdlog::info("CellEvoX Application finished run successfully");
}

void Application::update() { const float deltaTime = calculateDeltaTime(); }

float Application::calculateDeltaTime() {
  static auto lastFrame = std::chrono::high_resolution_clock::now();
  auto currentFrame = std::chrono::high_resolution_clock::now();

  float deltaTime = std::chrono::duration<float>(currentFrame - lastFrame).count();
  lastFrame = currentFrame;

  return deltaTime;
}

}  // namespace CellEvoX::core
