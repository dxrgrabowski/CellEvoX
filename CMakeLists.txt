cmake_minimum_required(VERSION 3.16)
project(CellEvoX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find required packages
find_package(Qt6 COMPONENTS Quick REQUIRED)
find_package(Boost REQUIRED)
find_package(Protobuf REQUIRED)
find_package(RocksDB REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(range-v3 REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(TBB REQUIRED)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)
