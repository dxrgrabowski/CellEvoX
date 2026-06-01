cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(CellEvoX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SKIP_GUI "Skip GUI and Qt dependencies" OFF)
option(CELLEVOX_PROFILE_PHASES "Enable opt-in CSV phase profiling instrumentation" OFF)
option(CELLEVOX_ENABLE_WARNINGS "Enable project compiler warnings" ON)
option(CELLEVOX_STRICT_WARNINGS "Enable extra noisy project warnings for local audits" OFF)
option(CELLEVOX_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(CELLEVOX_NATIVE_OPTIMIZATIONS "Enable native CPU tuning for the GUI target" ON)
option(CELLEVOX_FAST_MATH "Enable fast-math optimizations; may change floating-point behavior" OFF)

if(NOT SKIP_GUI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

# Find required packages
if(NOT SKIP_GUI)
    find_package(Qt6 6.4.2 COMPONENTS Quick QuickControls2 Qml Gui Core REQUIRED)
endif()
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
# Copy QML files to build directory
if(NOT SKIP_GUI)
    file(COPY qml DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()

function(cellevox_apply_project_warnings target_name)
    if(NOT CELLEVOX_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /wd4996)
        if(CELLEVOX_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wformat=2
            -Wno-deprecated-declarations
        )
        if(CELLEVOX_STRICT_WARNINGS)
            target_compile_options(${target_name} PRIVATE -Wshadow -Wconversion)
        endif()
        if(CELLEVOX_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(cellevox_apply_release_optimizations target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name} PRIVATE -O3)
        if(CELLEVOX_NATIVE_OPTIMIZATIONS)
            target_compile_options(${target_name} PRIVATE -march=native)
        endif()
        if(CELLEVOX_FAST_MATH)
            target_compile_options(${target_name} PRIVATE -ffast-math)
        endif()
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /O2)
    endif()
endfunction()

set(HEADERS
    include/core/application.hpp
    include/systems/SimulationEngine.hpp
    include/systems/SimulationEngine3D.hpp
    include/systems/SimulationEngine3DCapacity.hpp
    include/systems/CommonPopulationStep.hpp
   # include/core/DatabaseManager.hpp
    include/ecs/Cell.hpp
    include/ecs/Run.hpp
    include/spatial/SpatialHashGrid.hpp
    include/utils/MathUtils.hpp
    include/utils/PhaseProfiler.hpp
    include/utils/SimulationConfig.hpp
    include/core/RunDataEngine.hpp
    include/external/matplotlibcpp.h
)

set(SOURCES
    src/main.cpp
    src/core/application.cpp
    src/systems/SimulationEngine.cpp
    #src/core/DatabaseManager.cpp
    src/core/RunDataEngine.cpp
    src/ecs/Run.cpp
)


set(QML_FILES
    qml/main.qml
    qml/components/FileOperations.qml
    qml/components/ParameterInput.qml
    qml/components/ModernButton.qml
    qml/components/ModernTextField.qml
    qml/components/ModelSwitch.qml
    qml/components/ProgressBar.qml
    qml/components/GlassPanel.qml
    qml/components/GraphGenerator.qml
    qml/style/Themes.qml
)

if(NOT SKIP_GUI)
    qt6_standard_project_setup()
endif()

if(NOT SKIP_GUI)
    qt6_add_executable(CellEvoX
        ${SOURCES}
        ${HEADERS}
        MANUAL_FINALIZATION
    )

    qt6_add_qml_module(CellEvoX
        URI CellEvoX
        VERSION 1.0
        QML_FILES ${QML_FILES}
    )

    target_sources(CellEvoX PRIVATE ${QML_RESOURCES})
    target_sources(CellEvoX PRIVATE
        src/systems/SimulationEngine3D.cpp
        src/systems/SimulationEngine3DCapacity.cpp
        src/spatial/SpatialHashGrid.cpp
    )

    target_include_directories(CellEvoX
        PRIVATE
        include
        include/spatial
    )
    target_include_directories(CellEvoX SYSTEM
        PRIVATE
        ${Python3_INCLUDE_DIRS}
        ${NUMPY_INCLUDE_DIR}
    )

    cellevox_apply_release_optimizations(CellEvoX)
    cellevox_apply_project_warnings(CellEvoX)
    if(CELLEVOX_PROFILE_PHASES)
        target_compile_definitions(CellEvoX PRIVATE CELLEVOX_PROFILE_PHASES=1)
    endif()

    target_link_libraries(CellEvoX
        PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Qml
        Qt6::Core
        Qt6::Gui
        Boost::boost
        Boost::program_options
        #protobuf::libprotobuf
        #RocksDB::rocksdb
        spdlog::spdlog
        Eigen3::Eigen
        TBB::tbb
        nlohmann_json::nlohmann_json
        ${Python3_LIBRARIES}
    )
    if(WIN32)
        target_link_libraries(CellEvoX PRIVATE Psapi)
    endif()
endif()

if(NOT SKIP_GUI)
    install(TARGETS CellEvoX
        BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    qt_finalize_executable(CellEvoX)
endif()

# Testing Executable
add_executable(CellEvoXTests
    src/core/application.cpp
    src/systems/SimulationEngine.cpp
    src/systems/SimulationEngine3D.cpp
    src/systems/SimulationEngine3DCapacity.cpp
    src/core/RunDataEngine.cpp
    src/ecs/Run.cpp
    src/spatial/SpatialHashGrid.cpp
    tests/test_simulation.cpp
    tests/bench_simulation.cpp
)

target_include_directories(CellEvoXTests
    PRIVATE
    include
    include/spatial
)
target_include_directories(CellEvoXTests SYSTEM
    PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${NUMPY_INCLUDE_DIR}
)

cellevox_apply_project_warnings(CellEvoXTests)
if(CELLEVOX_PROFILE_PHASES)
    target_compile_definitions(CellEvoXTests PRIVATE CELLEVOX_PROFILE_PHASES=1)
endif()

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
if(WIN32)
    target_link_libraries(CellEvoXTests PRIVATE Psapi)
endif()

include(Catch)
catch_discover_tests(CellEvoXTests)
