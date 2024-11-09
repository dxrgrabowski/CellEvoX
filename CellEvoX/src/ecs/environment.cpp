#include "ecs/environment.hpp"
#include <spdlog/spdlog.h>
#include <tbb/parallel_for_each.h>

namespace CellEvoX::ecs {

Environment::Environment() {
    spdlog::info("ECS Environment created");
}

Environment::~Environment() = default;

void Environment::initialize() {
    //m_entityManager = std::make_unique<EntityManager>();
    spdlog::info("ECS Environment initialized");
}

void Environment::update(float deltaTime) {
    // Parallel execution of systems using TBB
    // tbb::parallel_for_each(m_systems.begin(), m_systems.end(),
    //     [deltaTime](auto& system) {
    //         system->update(deltaTime);
    //     }
    // );
}

// void Environment::addSystem(std::unique_ptr<System> system) {
//     m_systems.push_back(std::move(system));
//     spdlog::debug("New system added to ECS Environment");
// }

// EntityId Environment::createEntity() {
//     return m_entityManager->createEntity();
// }

// void Environment::destroyEntity(EntityId entity) {
//     m_entityManager->destroyEntity(entity);
// }

} // namespace CellEvoX::ecs
