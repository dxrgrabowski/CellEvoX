#include "core/application.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include "systems/SimulationEngine.hpp"
//#include "core/DatabaseManager.hpp"
namespace CellEvoX::core {

float calculateDeltaTime();

Application::Application()
{
    initialize();
}

Application::~Application() = default;

void Application::initialize() {
    spdlog::info("CellEvoX Application starting...");
    
    // Initialize the simulation engine
    SimulationEngine sim(SimulationEngine::SimulationType::STOCHASTIC_TAU_LEAP, 0.1, 1000);

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
