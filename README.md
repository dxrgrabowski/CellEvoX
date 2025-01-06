[![Docker](https://github.com/dxrgrabowski/CellEvoX/actions/workflows/docker-publish.yml/badge.svg)](https://github.com/dxrgrabowski/CellEvoX/actions/workflows/docker-publish.yml)
# CellEvoX

CellEvoX is a simulation system for modeling population dynamics, incorporating mutation types and probabilistic distributions. The project is designed with a modular and scalable architecture, leveraging modern C++ standards and libraries such as Qt6, TBB, and Eigen3 for high performance and ease of extensibility.

## Features

- **Stochastic and Deterministic Simulation**: Supports tau-leap simulation based on Gillespie's algorithm and provides extensibility for new methods.
- **Entity-Component-System (ECS) Architecture**: Decouples data and behavior for flexible simulation management.
- **Mutation Modeling**: Implements various mutation types with configurable probabilities and effects.
- **Statistical Reporting**: Generates detailed statistical snapshots, including mutation histograms and fitness distributions.
- **Graphical User Interface (GUI)**: Developed using QML for real-time visualization and user-friendly interaction.
- **Containerized Deployment**: Ensures reproducibility and ease of deployment using Docker.

## Requirements

- C++23 compatible compiler
- CMake 3.16 or higher
- Qt6 (Quick, QuickControls2, Qml, Core, Gui)
- Intel TBB
- Eigen3
- spdlog
- nlohmann\_json
- Python3 with NumPy (for additional utilities)

## Installation

### Using Docker

1. Log in to GitHub Container Registry:
   ```bash
   docker login ghcr.io
   ```
2. Pull the Docker image:
   ```bash
   docker pull ghcr.io/dxrgrabowski/cellevox:main
   ```
3. Run the container:
   ```bash
   docker run -it ghcr.io/dxrgrabowski/cellevox:main
   ```
4. Follow the instructions of builing on source

### Building Docker Image Locally

1. Clone the repository:
   ```bash
   git clone https://github.com/dxrgrabowski/cellevox.git
   cd cellevox
   ```
2. Build the Docker image:
   ```bash
   docker build -t cellevox:local .
   ```
3. Run the locally built container:
   ```bash
   docker run -it cellevox:local
   ```
4. Follow the instructions of builing on source

### Building from Source

1. Create a build directory and run CMake:
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```
2. Run the program:
   ```bash
   ./bin/CellEvoX
   ```

## Usage

1. Configure the simulation by editing the provided configuration file.
2. Run the simulation executable with --config pointing to config file localization.
3. View the generated data and statistical reports created in output directory specified in config. (Docker needs a write permission to output dir)

## File Structure

- **include/**: Header files defining core components.
- **src/**: Source files implementing the program logic.
- **qml/**: QML files for the GUI.
- **build/**: Directory for compiled binaries.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request with your changes.

## Contact

For questions or support, contact Dawid Grabowski at dxrsoftware@gmail.com.

