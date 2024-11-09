#pragma once

#include <memory>

namespace CellEvoX::core {

class Application {
public:
    Application();
    ~Application();

    void initialize();
    void update();
    float calculateDeltaTime();

private:

};

}
