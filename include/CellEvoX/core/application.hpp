#pragma once

#include <memory>
#include "CellEvoX/ecs/environment.hpp"

namespace CellEvoX::core {

class Application {
public:
    Application();
    ~Application();

    void initialize();
    void update();
    float calculateDeltaTime();

private:
    std::unique_ptr<ecs::Environment> m_environment;
};

}
