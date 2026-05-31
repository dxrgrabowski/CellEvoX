cmake_minimum_required(VERSION 3.16)
project(CellEvoX VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# Find required packages
find_package(Boost REQUIRED COMPONENTS program_options)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import numpy; print(numpy.get_include())"
    OUTPUT_VARIABLE NUMPY_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(spdlog REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(nlohmann_json 3 REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
include(GNUInstallDirs)

set(HEADERS
    include/core/application.hpp
    include/systems/SimulationEngine.hpp
    include/systems/SimulationEngine3D.hpp
    include/systems/SimulationEngine3DCapacity.hpp
    include/systems/CommonPopulationStep.hpp
    include/ecs/Cell.hpp
    include/ecs/Run.hpp
    include/spatial/SpatialHashGrid.hpp
    include/utils/MathUtils.hpp
    include/utils/SimulationConfig.hpp
    include/core/RunDataEngine.hpp
    include/external/matplotlibcpp.h
)

set(CORE_SOURCES
    src/core/application.cpp
    src/systems/SimulationEngine.cpp
    src/systems/SimulationEngine3D.cpp
    src/systems/SimulationEngine3DCapacity.cpp
    src/core/RunDataEngine.cpp
    src/ecs/Run.cpp
    src/spatial/SpatialHashGrid.cpp
)

add_executable(CellEvoX
    src/main.cpp
    ${CORE_SOURCES}
    ${HEADERS}
)

target_include_directories(CellEvoX
    PRIVATE
    include
    include/spatial
    ${Python3_INCLUDE_DIRS}
    ${NUMPY_INCLUDE_DIR}
)

target_compile_options(CellEvoX PRIVATE -Wno-deprecated-declarations)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(CellEvoX PRIVATE -O3 -march=native -ffast-math)
endif()

target_link_libraries(CellEvoX
    PRIVATE
    Boost::boost
    Boost::program_options
    spdlog::spdlog
    Eigen3::Eigen
    TBB::tbb
    nlohmann_json::nlohmann_json
    ${Python3_LIBRARIES}
)

install(TARGETS CellEvoX
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Testing Executable
add_executable(CellEvoXTests
    ${CORE_SOURCES}
    tests/test_simulation.cpp
    tests/bench_simulation.cpp
)

target_include_directories(CellEvoXTests
    PRIVATE
    include
    include/spatial
    ${Python3_INCLUDE_DIRS}
    ${NUMPY_INCLUDE_DIR}
)

# Suppress deprecation warnings from external header matplotlibcpp.h
# which uses Py_SetProgramName/PySys_SetArgv deprecated since Python 3.11
target_compile_options(CellEvoXTests PRIVATE -Wno-deprecated-declarations)

# Inject repo source directory for robust test data discovery (e.g. JSON baselines)
target_compile_definitions(CellEvoXTests PRIVATE "CELLEVOX_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

target_link_libraries(CellEvoXTests
    PRIVATE
    Boost::boost
    Boost::program_options
    spdlog::spdlog
    Eigen3::Eigen
    TBB::tbb
    nlohmann_json::nlohmann_json
    ${Python3_LIBRARIES}
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(CellEvoXTests)
