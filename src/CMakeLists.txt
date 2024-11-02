set(SOURCES
    main.cpp
    core/application.cpp
    ecs/environment.cpp
)

set(HEADERS
    ${CMAKE_SOURCE_DIR}/include/CellEvoX/core/application.hpp
    ${CMAKE_SOURCE_DIR}/include/CellEvoX/ecs/environment.hpp
)

add_executable(CellEvoX
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(CellEvoX
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(CellEvoX
    PRIVATE
    Qt6::Quick
    Boost::boost
    protobuf::libprotobuf
    RocksDB::rocksdb
    spdlog::spdlog
    fmt::fmt
    Eigen3::Eigen
    TBB::tbb
)
