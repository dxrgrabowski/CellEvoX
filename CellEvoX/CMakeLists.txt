set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# Copy QML files to build directory
file(COPY qml DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

set(SOURCES
    src/main.cpp
    src/core/application.cpp
    src/ecs/environment.cpp
)

set(HEADERS
    include/core/application.hpp
    include/ecs/environment.hpp
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
    qml/style/Themes.qml
)

qt6_standard_project_setup()

qt6_add_executable(CellEvoX_GUI
    ${SOURCES}
    ${HEADERS}
    MANUAL_FINALIZATION
)

qt6_add_qml_module(CellEvoX_GUI
    URI CellEvoX
    VERSION 1.0
    QML_FILES ${QML_FILES}
)

target_sources(CellEvoX_GUI PRIVATE ${QML_RESOURCES})

target_include_directories(CellEvoX_GUI
    PRIVATE
    include
)

target_link_libraries(CellEvoX_GUI
    PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Qml
    Qt6::Core
    Qt6::Gui
    Boost::boost
    protobuf::libprotobuf
    RocksDB::rocksdb
    spdlog::spdlog
    fmt::fmt
    Eigen3::Eigen
    TBB::tbb
)

install(TARGETS CellEvoX_GUI
	BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(CellEvoX_GUI)