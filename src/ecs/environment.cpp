#pragma once

#include <tsl/robin_map.h>
#include <memory>
#include <vector>

namespace CellEvoX::ecs {

class System;
class EntityManager;

class Environment {
public:
    Environment();
    ~Environment();

    void update(float deltaTime);
    void initialize();

private:
    std::unique_ptr<EntityManager> m_entityManager;
    std::vector<std::unique_ptr<System>> m_systems;
};

}
