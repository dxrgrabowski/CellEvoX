#pragma once

#include <memory>
#include "cancer_sim/ecs/world.hpp"

namespace cancer_sim {

class Application {
public:
    Application();
    ~Application();

    void initialize();
    void update();

private:
    std::unique_ptr<ecs::World> m_world;
};

}
