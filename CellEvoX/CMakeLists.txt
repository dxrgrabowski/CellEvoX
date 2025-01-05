cmake_minimum_required(VERSION 3.16)
project(CellEvoX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find required packages
find_package(Qt6 6.6.2 COMPONENTS Quick QuickControls2 Qml Gui Core REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
execute_process(
    COMMAND python3 -c "import numpy; print(numpy.get_include())"
    OUTPUT_VARIABLE NUMPY_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(spdlog REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(nlohmann_json 3 REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# Copy QML files to build directory
file(COPY qml DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

set(HEADERS
    include/core/application.hpp
    include/systems/SimulationEngine.hpp
   # include/core/DatabaseManager.hpp
    include/ecs/Cell.hpp
    include/ecs/Run.hpp
    include/utils/MathUtils.hpp
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

qt6_standard_project_setup()

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

target_include_directories(CellEvoX
    PRIVATE
    include
    ${Python3_INCLUDE_DIRS}
    ${NUMPY_INCLUDE_DIR}
)

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

install(TARGETS CellEvoX
	BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(CellEvoX)