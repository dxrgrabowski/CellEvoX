#include "core/application.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace CellEvoX::core {

float calculateDeltaTime();

Application::Application()
    : m_environment(std::make_unique<ecs::Environment>())
{
    initialize();
}

Application::~Application() = default;

void Application::initialize() {
    spdlog::info("CellEvoX Application starting...");
    
    // Initialize core systems
    // m_environment->initialize();
    
    // Register basic systems
    // registerSystems();
    
    spdlog::info("CellEvoX Application initialized successfully");
}

void Application::update() {
    const float deltaTime = calculateDeltaTime();
    m_environment->update(deltaTime);
}

float Application::calculateDeltaTime() {
    static auto lastFrame = std::chrono::high_resolution_clock::now();
    auto currentFrame = std::chrono::high_resolution_clock::now();
    
    float deltaTime = std::chrono::duration<float>(currentFrame - lastFrame).count();
    lastFrame = currentFrame;
    
    return deltaTime;
}

} // namespace cellevox::core
