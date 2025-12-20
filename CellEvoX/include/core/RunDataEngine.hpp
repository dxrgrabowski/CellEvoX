#pragma once

#include <tbb/concurrent_vector.h>

#include <fstream>
#include <string>

#include "ecs/Run.hpp"
#include "utils/SimulationConfig.hpp"

namespace CellEvoX::core {

class RunDataEngine {
 public:
  RunDataEngine(std::shared_ptr<SimulationConfig>,
                std::shared_ptr<ecs::Run>,
                const std::string& config_file_path,
                double generation_step = 0.005);

  // Generate a graph in Graphviz DOT format
  void prepareOutputDir();
  void exportToCSV();
  void plotLivingCellsOverGenerations();
  void plotFitnessStatistics();
  void plotMutationsStatistics();
  void plotMutationWave();
  void plotMutationFrequency();
  void exportGenealogyToGexf(size_t num_cells_to_trace, const std::string& filename);
  void exportPhylogeneticTreeToGEXF(const std::string& filename);
  // void exportToCSV(const tbb::concurrent_vector<Cell>& cells, const std::string& output_file);
 private:
  double generation_step;  // Time step for separating generations
  std::shared_ptr<SimulationConfig> config;
  std::shared_ptr<ecs::Run> run;
  std::string output_dir;
  std::string config_file_path;
  // Helper to classify cells into layers by generation
  // std::vector<std::vector<const Cell*>> classifyByGeneration(const tbb::concurrent_vector<Cell>&
  // cells);
};

}  // namespace CellEvoX::core