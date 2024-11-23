#include "core/application.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include "systems/SimulationEngine.hpp"
#include "utils/SimulationConfig.hpp"
#include "ecs/Run.hpp"
#include "core/RunDataEngine.hpp"
//#include "core/DatabaseManager.hpp"
namespace CellEvoX::core {

float calculateDeltaTime();

Application::Application(po::variables_map& vm) : vm(vm)
{
    initialize();
}

Application::~Application() = default;

void Application::initialize() {
    spdlog::info("CellEvoX Application starting...");
    spdlog::set_level(spdlog::level::trace);
    // Initialize the simulation engine
    if (vm.count("config")) {
        std::ifstream config_file(vm["config"].as<std::string>());
        nlohmann::json config;
        config_file >> config;
        sim_engine = std::make_unique<SimulationEngine>(utils::fromJson(config));
        const ecs::Run &run = sim_engine->run(config.at("steps"));

        // RunDataEngine data_engine(0.005);
        // data_engine.exportToGEXF(run.cells, "output.gexf", run.tau);
        // data_engine.exportToCSV(run.cells, "output.csv");
    }


    // Initialize the database manager
    // DatabaseManager db;
    
    spdlog::info("CellEvoX Application initialized successfully");
}

void Application::update() {
    const float deltaTime = calculateDeltaTime();
}

float Application::calculateDeltaTime() {
    static auto lastFrame = std::chrono::high_resolution_clock::now();
    auto currentFrame = std::chrono::high_resolution_clock::now();
    
    float deltaTime = std::chrono::duration<float>(currentFrame - lastFrame).count();
    lastFrame = currentFrame;
    
    return deltaTime;
}

} // namespace cellevox::core
