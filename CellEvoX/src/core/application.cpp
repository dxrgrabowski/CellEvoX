#include "core/application.hpp"

#include <spdlog/spdlog.h>

#include <fstream>
#include <csignal>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "core/RunDataEngine.hpp"
#include "ecs/Run.hpp"
#include "systems/SimulationEngine.hpp"
#include "systems/SimulationEngine3D.hpp"
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
  if (vm.count("analyze")) {
    std::string analyze_path = vm["analyze"].as<std::string>();
    spdlog::info("Running in analysis mode for directory: {}", analyze_path);
    
    // Initialize DataEngine in post-processing mode
    RunDataEngine data_engine(analyze_path);
    
    bool has_population_csv = false;
    bool has_population_bin = false;
    const std::filesystem::path population_dir =
        std::filesystem::path(analyze_path) / "population_data";
    if (std::filesystem::exists(population_dir)) {
      for (const auto& entry : std::filesystem::directory_iterator(population_dir)) {
        if (!entry.is_regular_file()) {
          continue;
        }
        const auto extension = entry.path().extension();
        has_population_csv = has_population_csv || extension == ".csv";
        has_population_bin = has_population_bin || extension == ".bin";
      }
    }

    if (has_population_csv) {
      data_engine.plotMullerDiagram();
      data_engine.plotClonePhylogenyTree();
      data_engine.plotCloneCounts();
      data_engine.plotCloneLifespans();
      data_engine.plotCloneGrowthAnimation();
      data_engine.plotTumorReplay3D();
    } else if (has_population_bin) {
      data_engine.exportPopulationSnapshotsToCSV();
      spdlog::info(
          "Detected binary population snapshots only; exported companion CSV files with driver mutation payloads when available.");
      data_engine.plotMullerDiagram();
      data_engine.plotClonePhylogenyTree();
      data_engine.plotCloneCounts();
      data_engine.plotCloneLifespans();
      data_engine.plotCloneGrowthAnimation();
      data_engine.plotTumorReplay3D();
    }
    
    spdlog::info("Analysis complete.");
  } else if (vm.count("config")) {
    std::ifstream config_file(vm["config"].as<std::string>());
    nlohmann::json config;
    config_file >> config;
    sim_config = std::make_shared<SimulationConfig>(utils::fromJson(config));
    utils::printConfig(*sim_config);
    std::string config_path = vm["config"].as<std::string>();
    
    // Initialize DataEngine first to prepare output directory
    RunDataEngine data_engine(sim_config, nullptr, config_path, 0.005);
    
    if (sim_config->sim_type == SimulationType::SPATIAL_3D_ABM) {
      sim_engine_3d = std::make_unique<SimulationEngine3D>(sim_config);
      std::signal(SIGINT, SimulationEngine3D::signalHandler);
      std::signal(SIGTERM, SimulationEngine3D::signalHandler);
      runs.push_back(std::make_shared<ecs::Run>(sim_engine_3d->run(config.at("steps"))));
    } else {
      sim_engine = std::make_unique<SimulationEngine>(sim_config);
      std::signal(SIGINT, SimulationEngine::signalHandler);
      std::signal(SIGTERM, SimulationEngine::signalHandler);
      runs.push_back(std::make_shared<ecs::Run>(sim_engine->run(config.at("steps"))));
    }
    
    // Set the run result in data engine
    data_engine.setRun(runs[0]);

    data_engine.plotFitnessStatistics();
    data_engine.plotMutationsStatistics();
    data_engine.plotLivingCellsOverGenerations();
    data_engine.exportToCSV();
    data_engine.exportPhylogeneticTreeToGEXF("phylogenetic.gexf");
    if (sim_config->sim_type != SimulationType::SPATIAL_3D_ABM) {
      data_engine.plotMutationWave();
      data_engine.plotMutationFrequency();
    } else {
      spdlog::info(
          "Spatial 3D mode stores driver-only mutation payloads in binary snapshots; skipping full-mutation plots.");
    }
    data_engine.plotMullerDiagram();
    data_engine.plotClonePhylogenyTree();
    data_engine.plotCloneCounts();
    data_engine.plotCloneLifespans();
    data_engine.plotCloneGrowthAnimation();
    data_engine.plotTumorReplay3D();
  } else {
    spdlog::error("Neither --config nor --analyze flag was provided. Please provide one.");
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
