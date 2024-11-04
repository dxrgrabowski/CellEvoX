set(SOURCES
    main.cpp
    core/application.cpp
    ecs/environment.cpp
)

set(HEADERS
    ${CMAKE_SOURCE_DIR}/include/CellEvoX/core/application.hpp
    ${CMAKE_SOURCE_DIR}/include/CellEvoX/ecs/environment.hpp
)

qt_add_resources(QML_RESOURCES ../resources/qml.qrc)

qt_add_executable(CellEvoX
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(CellEvoX
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_sources(CellEvoX PRIVATE ${QML_RESOURCES})

target_link_libraries(CellEvoX
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
