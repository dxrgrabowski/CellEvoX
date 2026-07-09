#pragma once

#include <tbb/concurrent_vector.h>

#include <fstream>
#include <string>
#include <vector>

#include "ecs/Run.hpp"
#include "systems/SimulationEngine.hpp"
#include "utils/SimulationConfig.hpp"

namespace CellEvoX::core {

class RunDataEngine {
 public:
  RunDataEngine(std::shared_ptr<SimulationConfig> config,
                std::shared_ptr<ecs::Run> run,
                const std::string& config_file_path,
                double generation_step = 0.005);
                
  RunDataEngine(const std::string& analyze_directory);

  void setRun(std::shared_ptr<ecs::Run> run);

  // Generate a graph in Graphviz DOT format
  void prepareOutputDir();
  void exportToCSV();
  void exportPopulationSnapshotsToCSV();
  void plotLivingCellsOverGenerations();
  void plotFitnessStatistics();
  void plotMutationsStatistics();
  void plotMutationWave();
  void plotMutationFrequency();
  void plotMullerDiagram();
  void plotClonePhylogenyTree();
  void plotCloneCounts();
  void plotCloneLifespans();
  void plotCloneGrowthAnimation();
  void plotTumorReplay3D();
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

  // Reconstructs generational statistics from statistics/generational_statistics.csv on disk.
  // Used as a fallback when no live ecs::Run object is available (e.g. `--analyze` mode on a
  // previously-completed run directory), so general_plots/* can still be regenerated purely
  // from exported artifacts.
  std::vector<StatSnapshot> loadGenerationalStatsFromCsv() const;

  // Returns run->generational_stat_report when a live run is available and non-empty, otherwise
  // falls back to loadGenerationalStatsFromCsv().
  std::vector<StatSnapshot> resolveGenerationalStats() const;
};

}  // namespace CellEvoX::core