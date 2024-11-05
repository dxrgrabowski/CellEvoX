cmake_minimum_required(VERSION 3.16)
project(CellEvoX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find required packages
find_package(Qt6 6.6.2 COMPONENTS Quick QuickControls2 Qml Gui Core REQUIRED)
find_package(Boost REQUIRED)
find_package(Protobuf REQUIRED)
find_package(RocksDB REQUIRED)
find_package(spdlog REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(TBB CONFIG REQUIRED)

# range-v3 included in C++23
# fmt included in C++23

# Add subdirectories
add_subdirectory(src)

# Create resources file


# add_subdirectory(tests)
