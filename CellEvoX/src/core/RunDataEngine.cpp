#include "core/RunDataEngine.hpp"

#include <external/matplotlibcpp.h>
#include <spdlog/spdlog.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <regex>
#include <sstream>

#include "io/PopulationSnapshotIO.hpp"

namespace {

namespace fs = std::filesystem;

constexpr const char* kPopulationCsvHeader =
    "CellID,ParentID,Fitness,MutationCount,Mutations,X,Y,Z,PositionValid,SpatialDimensions\n";

int extractGenerationFromFilename(const fs::path& path) {
  static const std::regex pattern(R"(population_generation_(\d+)\.(csv|bin))");
  std::smatch match;
  const std::string filename = path.filename().string();
  if (std::regex_match(filename, match, pattern)) {
    return std::stoi(match[1].str());
  }
  return -1;
}

std::string formatMutationsForCsv(const Cell& cell) {
  std::string mutations_str;
  for (const auto& [mutation_id, mutation_type] : cell.mutations) {
    mutations_str +=
        "(" + std::to_string(mutation_id) + "," + std::to_string(mutation_type) + ") ";
  }

  if (!mutations_str.empty()) {
    mutations_str.pop_back();
  }

  return mutations_str;
}

bool isDriverMutationType(const std::map<uint8_t, MutationType>& mutation_types, uint8_t mutation_type) {
  const auto it = mutation_types.find(mutation_type);
  return it != mutation_types.end() && it->second.is_driver;
}

std::string formatDriverMutationsForCsv(const Cell& cell,
                                        const std::map<uint8_t, MutationType>& mutation_types) {
  std::string mutations_str;
  for (const auto& [mutation_id, mutation_type] : cell.mutations) {
    if (!isDriverMutationType(mutation_types, mutation_type)) {
      continue;
    }
    mutations_str +=
        "(" + std::to_string(mutation_id) + "," + std::to_string(mutation_type) + ") ";
  }

  if (!mutations_str.empty()) {
    mutations_str.pop_back();
  }

  return mutations_str;
}

std::string formatDriverMutationsForCsv(
    const CellEvoX::io::PopulationSnapshotRecord& record,
    const std::vector<CellEvoX::io::PopulationSnapshotDriverMutation>& driver_mutations) {
  if (record.driver_mutation_count == 0) {
    return "";
  }

  const size_t start = record.driver_mutation_offset;
  const size_t end = start + record.driver_mutation_count;
  if (start > driver_mutations.size() || end > driver_mutations.size()) {
    return "";
  }

  std::string mutations_str;
  for (size_t i = start; i < end; ++i) {
    const auto& mutation = driver_mutations[i];
    mutations_str +=
        "(" + std::to_string(mutation.mutation_id) + "," + std::to_string(mutation.mutation_type) +
        ") ";
  }

  if (!mutations_str.empty()) {
    mutations_str.pop_back();
  }

  return mutations_str;
}

void writePopulationCsvRow(std::ofstream& file,
                           uint32_t cell_id,
                           uint32_t parent_id,
                           float fitness,
                           uint32_t mutation_count,
                           const std::string& mutations,
                           bool position_valid,
                           float x,
                           float y,
                           float z,
                           uint8_t spatial_dimensions) {
  file << cell_id << "," << parent_id << "," << fitness << "," << mutation_count << ","
       << "\"" << mutations << "\",";
  if (position_valid) {
    file << x << "," << y << "," << z;
  } else {
    file << ",,";
  }
  file << "," << (position_valid ? 1 : 0) << "," << static_cast<int>(spatial_dimensions) << "\n";
}

std::vector<fs::path> collectPopulationBinaryFiles(const std::string& output_dir) {
  std::vector<fs::path> files;
  const fs::path population_dir = fs::path(output_dir) / "population_data";
  if (!fs::exists(population_dir)) {
    return files;
  }

  for (const auto& entry : fs::directory_iterator(population_dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".bin") {
      continue;
    }
    if (extractGenerationFromFilename(entry.path()) >= 0) {
      files.push_back(entry.path());
    }
  }

  std::sort(files.begin(), files.end(), [](const fs::path& lhs, const fs::path& rhs) {
    return extractGenerationFromFilename(lhs) < extractGenerationFromFilename(rhs);
  });
  return files;
}

std::string quoteForShell(const std::string& value) { return "\"" + value + "\""; }

fs::path resolvePythonScriptPath(const char* script_name) {
  fs::path script_path = fs::current_path() / "scripts" / script_name;
  if (fs::exists(script_path)) {
    return script_path;
  }

  script_path = fs::path(__FILE__).parent_path().parent_path() / "scripts" / script_name;
  if (fs::exists(script_path)) {
    return script_path;
  }

  script_path = fs::path("/workspaces/CellEvoX/CellEvoX/scripts") / script_name;
  if (fs::exists(script_path)) {
    return script_path;
  }

  return fs::current_path() / "../scripts" / script_name;
}

std::string resolvePythonCommand() {
  fs::path venv_python = "/workspaces/CellEvoX/.venv/bin/python";
  if (fs::exists(venv_python)) {
    return venv_python.string();
  }
  return "python3";
}

}  // namespace

namespace CellEvoX::core {

namespace plt = matplotlibcpp;

RunDataEngine::RunDataEngine(std::shared_ptr<SimulationConfig> config,
                             std::shared_ptr<ecs::Run> run,
                             const std::string& config_file_path,
                             double generation_step)
    : config(config),
      run(run),
      config_file_path(config_file_path),
      generation_step(generation_step) {
  prepareOutputDir();
}

RunDataEngine::RunDataEngine(const std::string& analyze_directory)
    : config(nullptr),
      run(nullptr),
      config_file_path(""),
      generation_step(0.0) {
  output_dir = analyze_directory;
  if (!output_dir.empty() && output_dir.back() != '/') {
    output_dir += '/';
  }      
}

void RunDataEngine::setRun(std::shared_ptr<ecs::Run> r) {
    this->run = r;
}

void RunDataEngine::prepareOutputDir() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S");
  std::string timestamp = ss.str();

  std::filesystem::path base_path = config->output_path;
  std::filesystem::path timestamped_path = base_path / timestamp;

  output_dir = timestamped_path.string();
  config->output_path = output_dir;

  if (!std::filesystem::exists(output_dir) && output_dir != "") {
    std::filesystem::create_directories(output_dir);
    // Create subdirectories
    std::vector<std::string> subdirs = {"vaf_diagrams",      "mutation_histograms",
                                        "population_data",   "general_plots",
                                        "muller_plots",      "phylogeny",
                                        "statistics"};
    for (const auto& subdir : subdirs) {
      std::filesystem::create_directories(std::filesystem::path(output_dir) / subdir);
    }
  }
  output_dir += "/";

  // Copy config file if it exists
  if (!config_file_path.empty() && std::filesystem::exists(config_file_path)) {
    try {
      std::filesystem::copy(config_file_path, timestamped_path / "config.json",
                            std::filesystem::copy_options::overwrite_existing);
      spdlog::info("Copied config file to: {}", (timestamped_path / "config.json").string());
    } catch (const std::filesystem::filesystem_error& e) {
      spdlog::error("Failed to copy config file: {}", e.what());
    }
  }
}
void RunDataEngine::exportToCSV() {
  // Export Generational Statistics
  {
    std::string statFilename = output_dir + "statistics/generational_statistics.csv";
    std::ofstream file(statFilename);
    if (!file.is_open()) {
      std::cerr << "Cannot open file: " << statFilename << std::endl;
    } else {
      // Write header row with all fields
      file << "Generation,TotalLivingCells,MeanFitness,FitnessVariance,FitnessSkewness,"
              "FitnessKurtosis,"
           << "MeanMutations,MutationsVariance,MutationsSkewness,MutationsKurtosis\n";

      // Write data rows
      for (size_t generation = 0; generation < run->generational_stat_report.size(); ++generation) {
        const auto& stat = run->generational_stat_report[generation];
        file << stat.tau << "," << stat.total_living_cells << "," << stat.mean_fitness << ","
             << stat.fitness_variance << "," << stat.fitness_skewness << ","
             << stat.fitness_kurtosis << "," << stat.mean_mutations << ","
             << stat.mutations_variance << "," << stat.mutations_skewness << ","
             << stat.mutations_kurtosis << "\n";
      }

      file.close();
      std::cout << "Generational stats exported to: " << statFilename << std::endl;
    }
  }

  exportPopulationSnapshotsToCSV();

  {
    std::string phylogeneticFilename = output_dir + "phylogeny/phylogenetic_tree.csv";
    std::ofstream file(phylogeneticFilename);

    if (!file.is_open()) {
      std::cerr << "Cannot open file: " << phylogeneticFilename << std::endl;
      return;
    }

    file << "NodeID,ParentID,ChildSum,DeathTime\n";

    for (const auto& [node_id, node_data] : run->phylogenetic_tree) {
      file << node_id << "," << node_data.parent_id << "," << node_data.child_sum << ","
           << node_data.death_time << "\n";
    }

    file.close();
  }
}

void RunDataEngine::exportPopulationSnapshotsToCSV() {
  if (run && !run->generational_popul_report.empty()) {
    for (const auto& [generation, cell_map] : run->generational_popul_report) {
      const std::string populFilename =
          output_dir + "population_data/population_generation_" + std::to_string(generation) + ".csv";
      std::ofstream file(populFilename);
      if (!file.is_open()) {
        std::cerr << "Cannot open file: " << populFilename << std::endl;
        continue;
      }

      file << kPopulationCsvHeader;
      for (const auto& [cell_id, cell_data] : cell_map) {
        writePopulationCsvRow(
            file,
            cell_id,
            cell_data.parent_id,
            cell_data.fitness,
            static_cast<uint32_t>(
                std::min<size_t>(cell_data.mutations.size(), std::numeric_limits<uint32_t>::max())),
            formatDriverMutationsForCsv(cell_data, run->mutation_id_to_type),
            false,
            0.0f,
            0.0f,
            0.0f,
            0);
      }

      std::cout << "Population data exported to: " << populFilename << std::endl;
    }
    return;
  }

  for (const auto& path : collectPopulationBinaryFiles(output_dir)) {
    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> records;
    std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> driver_mutations;
    if (!CellEvoX::io::readPopulationSnapshot(path, header, records, driver_mutations)) {
      spdlog::error("Failed to read population snapshot file: {}", path.string());
      continue;
    }

    const int generation = extractGenerationFromFilename(path);
    const std::string csv_filename =
        (path.parent_path() / ("population_generation_" + std::to_string(generation) + ".csv")).string();
    std::ofstream file(csv_filename);
    if (!file.is_open()) {
      std::cerr << "Cannot open file: " << csv_filename << std::endl;
      continue;
    }

    file << kPopulationCsvHeader;
    for (const auto& record : records) {
      writePopulationCsvRow(file,
                            record.id,
                            record.parent_id,
                            record.fitness,
                            record.mutations_count,
                            formatDriverMutationsForCsv(record, driver_mutations),
                            record.position_valid != 0,
                            record.x,
                            record.y,
                            record.z,
                            header.spatial_dimensions);
    }

    std::cout << "Population data exported to: " << csv_filename << std::endl;
  }
}
void RunDataEngine::plotLivingCellsOverGenerations() {
  std::vector<double> generations;
  std::vector<size_t> living_cells;

  for (const auto& snapshot : run->generational_stat_report) {
    generations.push_back(snapshot.tau);
    living_cells.push_back(snapshot.total_living_cells);
  }

  plt::figure_size(800, 600);
  plt::plot(generations, living_cells, "g-");
  plt::xlabel("Generation");
  plt::ylabel("Total Living Cells");
  plt::title("Number of Living Cells Over Generations");
  plt::grid(true);  // Dodaj siatkę
  plt::save(output_dir + "general_plots/living_cells_over_generations.png");
  plt::close();
}
void RunDataEngine::plotFitnessStatistics() {
  std::vector<double> generations;
  std::vector<double> mean_fitness;
  std::vector<double> fitness_variance;
  std::vector<double> fitness_skewness;
  std::vector<double> fitness_kurtosis;

  for (const auto& snapshot : run->generational_stat_report) {
    generations.push_back(snapshot.tau);
    mean_fitness.push_back(snapshot.mean_fitness);
    fitness_variance.push_back(snapshot.fitness_variance);
    fitness_skewness.push_back(snapshot.fitness_skewness);
    fitness_kurtosis.push_back(snapshot.fitness_kurtosis);
  }

  plt::figure_size(800, 600);
  plt::plot(generations, mean_fitness, {{"label", "Mean Fitness (χs(t))"}});
  plt::xlabel("Generation");
  plt::ylabel("Mean Fitness");
  plt::title("Mean Fitness Over Generations");
  plt::legend();
  plt::grid(true);
  plt::save(output_dir + "general_plots/mean_fitness_over_generations.png");
  plt::close();

  plt::figure_size(800, 600);
  plt::plot(generations, fitness_variance, {{"label", "Fitness Variance (σs²(t))"}});
  plt::xlabel("Generation");
  plt::ylabel("Fitness Variance");
  plt::title("Fitness Variance Over Generations");
  plt::legend();
  plt::grid(true);
  plt::save(output_dir + "general_plots/fitness_variance_over_generations.png");
  plt::close();

  plt::figure_size(800, 600);
  plt::plot(generations, fitness_skewness, {{"label", "Fitness Skewness"}});
  plt::xlabel("Generation");
  plt::ylabel("Skewness");
  plt::title("Fitness Skewness Over Generations");
  plt::legend();
  plt::grid(true);
  plt::save(output_dir + "general_plots/fitness_skewness_over_generations.png");
  plt::close();

  plt::figure_size(800, 600);
  plt::plot(generations, fitness_kurtosis, {{"label", "Fitness Kurtosis"}});
  plt::xlabel("Generation");
  plt::ylabel("Kurtosis");
  plt::title("Fitness Kurtosis Over Generations");
  plt::legend();
  plt::grid(true);
  plt::save(output_dir + "general_plots/fitness_kurtosis_over_generations.png");
  plt::close();
}

void RunDataEngine::plotMutationsStatistics() {
  std::vector<double> generations;
  std::vector<double> mean_mutations;
  std::vector<double> mutations_variance;
  std::vector<double> mutations_skewness;
  std::vector<double> mutations_kurtosis;

  for (const auto& snapshot : run->generational_stat_report) {
    generations.push_back(snapshot.tau);
    mean_mutations.push_back(snapshot.mean_mutations);
    mutations_variance.push_back(snapshot.mutations_variance);
    mutations_skewness.push_back(snapshot.mutations_skewness);
    mutations_kurtosis.push_back(snapshot.mutations_kurtosis);
  }

  plt::figure_size(800, 600);
  plt::plot(generations, mean_mutations, {{"label", "Mean Mutations (χs(t))"}});
  plt::xlabel("Generation");
  plt::ylabel("Mean Mutations");
  plt::title("Mean Mutations Over Generations");
  plt::legend();
  plt::grid(true);
  plt::save(output_dir + "general_plots/mean_mutations_over_generations.png");
  plt::close();

  plt::figure_size(800, 600);
  plt::plot(generations, mutations_variance, {{"label", "Mutations Variance (σs²(t))"}});
  plt::xlabel("Generation");
  plt::ylabel("Mutations Variance");
  plt::title("Mutations Variance Over Generations");
  plt::legend();
  plt::grid(true);
  plt::save(output_dir + "general_plots/mutations_variance_over_generations.png");
  plt::close();

  // Plot Mutations Skewness
  plt::figure_size(800, 600);
  plt::plot(generations, mutations_skewness, {{"label", "Mutations Skewness"}});
  plt::xlabel("Generation");
  plt::ylabel("Skewness");
  plt::title("Mutations Skewness Over Generations");
  plt::legend();
  plt::grid(true);
  plt::save(output_dir + "general_plots/mutations_skewness_over_generations.png");
  plt::close();

  // Plot Mutations Kurtosis
  plt::figure_size(800, 600);
  plt::plot(generations, mutations_kurtosis, {{"label", "Mutations Kurtosis"}});
  plt::xlabel("Generation");
  plt::ylabel("Kurtosis");
  plt::title("Mutations Kurtosis Over Generations");
  plt::legend();
  plt::grid(true);
  plt::save(output_dir + "general_plots/mutations_kurtosis_over_generations.png");
  plt::close();
}

void RunDataEngine::plotMutationWave() {
  for (const auto& [generation, cells] : run->generational_popul_report) {
    std::map<size_t, size_t> mutation_counts;  // <number of mutations, number of cells>

    for (const auto& cell : cells) {
      size_t num_mutations = cell.second.mutations.size();
      mutation_counts[num_mutations]++;
    }

    std::vector<size_t> mutation_bins;
    std::vector<size_t> cell_counts;

    for (const auto& [mutations, count] : mutation_counts) {
      mutation_bins.push_back(mutations);
      cell_counts.push_back(count);
    }

    plt::figure_size(1000, 600);
    plt::bar(mutation_bins, cell_counts, "green");
    plt::xlabel("Number of Mutations");
    plt::ylabel("Number of Cells");
    plt::title("Mutation Wave: Distribution of Mutation Counts (Generation " +
               std::to_string(generation) + ")");
    plt::grid(true);
    plt::save(output_dir + "mutation_histograms/mutation_wave_histogram_generation_" +
              std::to_string(generation) + ".png");
    plt::close();
  }
}

void RunDataEngine::plotMutationFrequency() {
  for (const auto& [generation, cells] : run->generational_popul_report) {
    std::map<uint32_t, uint32_t> mutation_counts;
    uint32_t total_cells = 0;

    for (const auto& item : cells) {
      const Cell& cell = item.second;
      ++total_cells;

      for (const auto& mutation : cell.mutations) {
        uint32_t mutation_id = mutation.first;
        mutation_counts[mutation_id]++;
      }
    }

    std::vector<double> vafs;
    for (const auto& [mutation_id, count] : mutation_counts) {
      double vaf = static_cast<double>(count) / total_cells;
      vafs.push_back(vaf);
    }

    int num_bins = std::max(1, static_cast<int>(std::ceil(1 + 3.322 * std::log10(vafs.size()))));

    plt::figure();
    plt::hist(vafs, num_bins);
    plt::title("VAF Histogram - Generation " + std::to_string(generation));
    plt::xlabel("Variant Allele Frequency (VAF)");
    plt::ylabel("Frequency");
    plt::save(output_dir + "vaf_diagrams/vaf_histogram_generation_" + std::to_string(generation) +
              ".png");
    plt::close();
  }
}

void RunDataEngine::exportPhylogeneticTreeToGEXF(const std::string& filename) {
  std::string output_file = output_dir + "phylogeny/" + filename;
  std::ofstream file(output_file);

  if (!file.is_open()) {
    std::cerr << "Nie można otworzyć pliku: " << output_file << std::endl;
    return;
  }

  file << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n";
  file << R"(<gexf xmlns="http://www.gexf.net/1.3" version="1.3">)" << "\n";
  file << R"(<graph mode="static" defaultedgetype="directed">)" << "\n";

  file << R"(<attributes class="node">)" << "\n";
  file << R"(<attribute id="0" title="status" type="string"/>)" << "\n";
  file << R"(<attribute id="2" title="parent_id" type="integer"/>)" << "\n";
  file << R"(<attribute id="3" title="child_sum" type="integer"/>)" << "\n";
  file << R"(</attributes>)" << "\n";

  file << R"(<nodes>)" << "\n";
  for (const auto& [node_id, node_data] : run->phylogenetic_tree) {
    CellMap::const_accessor cell_accessor;
    bool is_alive = run->cells.find(cell_accessor, node_id);
    std::string status = is_alive ? "ALIVE" : "DEAD";

    file << R"(<node id=")" << node_id << R"(" label="Node )" << node_id << R"(">)" << "\n";
    file << R"(<attvalues>)" << "\n";
    file << R"(<attvalue for="0" value=")" << status << R"("/>)" << "\n";
    file << R"(<attvalue for="2" value=")" << node_data.parent_id << R"("/>)" << "\n";
    file << R"(<attvalue for="3" value=")" << node_data.child_sum << R"("/>)" << "\n";
    file << R"(</attvalues>)" << "\n";
    file << R"(</node>)" << "\n";
  }
  file << R"(</nodes>)" << "\n";

  file << R"(<edges>)" << "\n";
  uint32_t edge_id = 0;
  for (const auto& [node_id, node_data] : run->phylogenetic_tree) {
    if (node_data.parent_id != 0) {
      file << R"(<edge id=")" << edge_id++ << R"(" source=")" << node_data.parent_id
           << R"(" target=")" << node_id << R"("/>)" << "\n";
    } else if (node_id != 0) {
      file << R"(<edge id=")" << edge_id++ << R"(" source=")" << 0 << R"(" target=")" << node_id
           << R"("/>)" << "\n";
    }
  }
  file << R"(</edges>)" << "\n";

  file << R"(</graph>)" << "\n";
  file << R"(</gexf>)" << "\n";

  file.close();
  std::cout << "Graf zapisany do pliku: " << output_file << std::endl;
}

void RunDataEngine::exportGenealogyToGexf(size_t num_cells_to_trace, const std::string& filename) {
  tbb::concurrent_vector<uint32_t> selected_cells;
  tbb::concurrent_unordered_set<uint32_t> visited_nodes;
  tbb::concurrent_vector<std::pair<uint32_t, uint32_t>> edges;
  tbb::concurrent_hash_map<uint32_t, std::pair<std::string, double>> node_attributes;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, run->cells.size() - 1);

  std::vector<uint32_t> cell_ids;
  for (auto it = run->cells.begin(); it != run->cells.end(); ++it) {
    cell_ids.push_back(it->first);
  }

  for (size_t i = 0; i < num_cells_to_trace; ++i) {
    uint32_t idx = dis(gen);
    selected_cells.push_back(cell_ids[idx]);
  }

  tbb::parallel_for(size_t(0), selected_cells.size(), [&](size_t i) {
    uint32_t current_id = selected_cells[i];
    while (current_id != 0) {
      uint32_t parent_id = 0;

      {
        CellMap::const_accessor cell_accessor;
        if (run->cells.find(cell_accessor, current_id)) {
          parent_id = cell_accessor->second.parent_id;

          tbb::concurrent_hash_map<uint32_t, std::pair<std::string, double>>::accessor
              attr_accessor;
          if (node_attributes.insert(attr_accessor, current_id)) {
            attr_accessor->second = {"alive", 0.0};
          }
        } else {
          Graveyard::const_accessor grave_accessor;
          if (run->cells_graveyard.find(grave_accessor, current_id)) {
            parent_id = grave_accessor->second.first;

            tbb::concurrent_hash_map<uint32_t, std::pair<std::string, double>>::accessor
                attr_accessor;
            if (node_attributes.insert(attr_accessor, current_id)) {
              attr_accessor->second = {"dead", grave_accessor->second.second};
            }
          }
        }
      }

      if (parent_id != 0) {
        edges.emplace_back(parent_id, current_id);
        visited_nodes.insert(current_id);
        visited_nodes.insert(parent_id);
      }

      current_id = parent_id;
    }
  });

  std::ofstream gexf_file(output_dir + "phylogeny/" + filename);
  if (!gexf_file.is_open()) {
    return;
  }

  gexf_file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  gexf_file << "<gexf xmlns=\"http://www.gexf.net/1.3\" version=\"1.3\">\n";
  gexf_file << "  <graph mode=\"static\" defaultedgetype=\"directed\">\n";
  gexf_file << "    <attributes class=\"node\">\n";
  gexf_file << "      <attribute id=\"0\" title=\"status\" type=\"string\"/>\n";
  gexf_file << "      <attribute id=\"1\" title=\"death_time\" type=\"double\"/>\n";
  gexf_file << "    </attributes>\n";
  gexf_file << "    <nodes>\n";
  for (const auto& node : visited_nodes) {
    std::string status = "unknown";
    double death_time = -1.0;

    tbb::concurrent_hash_map<uint32_t, std::pair<std::string, double>>::const_accessor
        attr_accessor;
    if (node_attributes.find(attr_accessor, node)) {
      status = attr_accessor->second.first;
      death_time = attr_accessor->second.second;
    }

    gexf_file << "      <node id=\"" << node << "\" label=\"Cell " << node << "\">\n";
    gexf_file << "        <attvalues>\n";
    gexf_file << "          <attvalue for=\"0\" value=\"" << status << "\"/>\n";
    gexf_file << "          <attvalue for=\"1\" value=\"" << death_time << "\"/>\n";
    gexf_file << "        </attvalues>\n";
    gexf_file << "      </node>\n";
  }
  gexf_file << "    </nodes>\n";
  gexf_file << "    <edges>\n";
  size_t edge_id = 0;
  for (const auto& edge : edges) {
    gexf_file << "      <edge id=\"" << edge_id++ << "\" source=\"" << edge.first << "\" target=\""
              << edge.second << "\"/>\n";
  }
  gexf_file << "    </edges>\n";
  gexf_file << "  </graph>\n";
  gexf_file << "</gexf>\n";
}

void RunDataEngine::plotMullerDiagram() {
  spdlog::info("Generating Müller diagram...");
  
  // Get the path to the Python script (relative to project structure)
  std::filesystem::path script_path = std::filesystem::current_path() / "scripts" / "plot_muller.py";
  
  // If not found, try relative to the executable
  if (!std::filesystem::exists(script_path)) {
    // Try finding it relative to the source directory
    script_path = std::filesystem::path(__FILE__).parent_path().parent_path() / "scripts" / "plot_muller.py";
  }
  
  // Fallback to absolute path if needed
  if (!std::filesystem::exists(script_path)) {
    script_path = "/workspaces/CellEvoX/CellEvoX/scripts/plot_muller.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    script_path = std::filesystem::current_path() / "../scripts/plot_muller.py";
  }

  if (!std::filesystem::exists(script_path)) {
    spdlog::warn("Müller plot script not found at any locations, last tried: {}", script_path.string());
    return;
  }
  
  // Try to use virtual environment Python if available, otherwise fall back to system python3
  std::string python_cmd = "python3";
  std::filesystem::path venv_python = "/workspaces/CellEvoX/.venv/bin/python";
  if (std::filesystem::exists(venv_python)) {
    python_cmd = venv_python.string();
  }
  
  // 1. Generate Relative Plot (Standard)
  std::string command_relative = python_cmd + " " + script_path.string() + 
                               " --input " + output_dir +
                               " --output " + output_dir + "muller_plots/muller_plot.png";
  
  spdlog::info("Running relative plot: {}", command_relative);
  int result_rel = std::system(command_relative.c_str());
  
  if (result_rel == 0) {
    spdlog::info("Relative Müller plot saved to: {}muller_plot.png", output_dir);
  } else {
    spdlog::warn("Relative Müller plot generation failed with code: {}", result_rel);
  }

  // 2. Generate Absolute Plot
  std::string command_absolute = python_cmd + " " + script_path.string() + 
                               " --input " + output_dir +
                               " --output " + output_dir + "muller_plots/muller_plot_absolute.png" +
                               " --absolute";
                               
  spdlog::info("Running absolute plot: {}", command_absolute);
  int result_abs = std::system(command_absolute.c_str());
  
  if (result_abs == 0) {
    spdlog::info("Absolute Müller plot saved to: {}muller_plot_absolute.png", output_dir);
  } else {
    spdlog::warn("Absolute Müller plot generation failed with code: {}", result_abs);
  }
}

void RunDataEngine::plotClonePhylogenyTree() {
  spdlog::info("Generating Clone Phylogeny Tree...");
  
  std::filesystem::path script_path = std::filesystem::current_path() / "scripts" / "plot_phylogeny.py";
  
  if (!std::filesystem::exists(script_path)) {
    script_path = std::filesystem::path(__FILE__).parent_path().parent_path() / "scripts" / "plot_phylogeny.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    script_path = "/workspaces/CellEvoX/CellEvoX/scripts/plot_phylogeny.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    script_path = std::filesystem::current_path() / "../scripts/plot_phylogeny.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    spdlog::warn("Phylogeny plot script not found at any locations, last tried: {}", script_path.string());
    return;
  }
  
  std::string python_cmd = "python3";
  std::filesystem::path venv_python = "/workspaces/CellEvoX/.venv/bin/python";
  if (std::filesystem::exists(venv_python)) {
    python_cmd = venv_python.string();
  }
  
  uint32_t num_cells = 100;
  if (config) {
    num_cells = config->phylogeny_num_cells_sampling;
  }

  // 1. Clone Phylogeny Tree (default mode)
  std::string clone_command = python_cmd + " " + script_path.string() + 
                            " --input " + output_dir +
                            " --output " + output_dir + "phylogeny/clone_tree.png";
                       
  spdlog::info("Running clone phylogeny plot: {}", clone_command);
  int result_clone = std::system(clone_command.c_str());
  
  if (result_clone == 0) {
    spdlog::info("Clone Phylogeny Tree saved to: {}phylogeny/clone_tree.png", output_dir);
  } else {
    spdlog::warn("Clone Phylogeny Tree generation failed with code: {}", result_clone);
  }

  // 2. Cell Phylogeny Tree (with --cells)
  std::string cell_command = python_cmd + " " + script_path.string() + 
                           " --input " + output_dir +
                           " --cells " + std::to_string(num_cells);
  
  spdlog::info("Running cell phylogeny plot: {}", cell_command);
  int result_cell = std::system(cell_command.c_str());
  
  if (result_cell == 0) {
    spdlog::info("Cell Phylogeny Tree (n={}) saved to: {}phylogeny/", num_cells, output_dir);
  } else {
    spdlog::warn("Cell Phylogeny Tree generation failed with code: {}", result_cell);
  }
}

void RunDataEngine::plotCloneCounts() {
  spdlog::info("Generating Clone Counts over time chart...");
  
  std::filesystem::path script_path = std::filesystem::current_path() / "scripts" / "plot_clone_counts.py";
  
  if (!std::filesystem::exists(script_path)) {
    script_path = std::filesystem::path(__FILE__).parent_path().parent_path() / "scripts" / "plot_clone_counts.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    script_path = "/workspaces/CellEvoX/CellEvoX/scripts/plot_clone_counts.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    script_path = std::filesystem::current_path() / "../scripts/plot_clone_counts.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    spdlog::warn("Clone counts script not found at any locations, last tried: {}", script_path.string());
    return;
  }
  
  std::string python_cmd = "python3";
  std::filesystem::path venv_python = "/workspaces/CellEvoX/.venv/bin/python";
  if (std::filesystem::exists(venv_python)) {
    python_cmd = venv_python.string();
  }
  
  std::string command = python_cmd + " " + script_path.string() + " --input " + output_dir;
                       
  spdlog::info("Running clone counts plot: {}", command);
  int result = std::system(command.c_str());
  
  if (result == 0) {
    spdlog::info("Clone counts chart saved to clones/ subdirectory.");
  } else {
    spdlog::warn("Clone counts chart generation failed with code: {}", result);
  }
}

void RunDataEngine::plotCloneLifespans() {
  spdlog::info("Generating Clone Lifespans Histogram Plot...");
  
  std::filesystem::path script_path = std::filesystem::current_path() / "scripts" / "plot_clone_lifespans.py";
  
  if (!std::filesystem::exists(script_path)) {
    script_path = std::filesystem::path(__FILE__).parent_path().parent_path() / "scripts" / "plot_clone_lifespans.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    script_path = "/workspaces/CellEvoX/CellEvoX/scripts/plot_clone_lifespans.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    script_path = std::filesystem::current_path() / "../scripts/plot_clone_lifespans.py";
  }
  
  if (!std::filesystem::exists(script_path)) {
    spdlog::warn("Clone lifespans script not found at any locations, last tried: {}", script_path.string());
    return;
  }
  
  std::string python_cmd = "python3";
  std::filesystem::path venv_python = "/workspaces/CellEvoX/.venv/bin/python";
  if (std::filesystem::exists(venv_python)) {
    python_cmd = venv_python.string();
  }
  
  std::string command = python_cmd + " " + script_path.string() + " --input " + output_dir;
                       
  spdlog::info("Running clone lifespans plot: {}", command);
  int result = std::system(command.c_str());
  
  if (result == 0) {
    spdlog::info("Clone lifespans charts saved to clones/ subdirectory.");
  } else {
    spdlog::warn("Clone lifespans chart generation failed with code: {}", result);
  }
}

void RunDataEngine::plotCloneGrowthAnimation() {
  spdlog::info("Generating animated 2D clone growth visualization...");

  std::filesystem::path script_path = resolvePythonScriptPath("animate_clone_growth_2d.py");
  if (!std::filesystem::exists(script_path)) {
    spdlog::warn("2D clone growth script not found at any locations, last tried: {}",
                 script_path.string());
    return;
  }

  const std::string python_cmd = resolvePythonCommand();
  const std::string command =
      python_cmd + " " + quoteForShell(script_path.string()) + " --input " +
      quoteForShell(output_dir) + " --output " +
      quoteForShell(output_dir + "visualizations/clone_growth_2d.mp4");

  spdlog::info("Running 2D clone growth animation: {}", command);
  const int result = std::system(command.c_str());
  if (result == 0) {
    spdlog::info("2D clone growth animation saved to: {}visualizations/clone_growth_2d.mp4",
                 output_dir);
  } else {
    spdlog::warn("2D clone growth animation failed with code: {}", result);
  }
}

void RunDataEngine::plotTumorReplay3D() {
  spdlog::info("Generating 3D tumor replay...");

  std::filesystem::path script_path = resolvePythonScriptPath("visualize_tumor_3d.py");
  if (!std::filesystem::exists(script_path)) {
    spdlog::warn("3D tumor replay script not found at any locations, last tried: {}",
                 script_path.string());
    return;
  }

  const std::string python_cmd = resolvePythonCommand();
  const std::string command =
      python_cmd + " " + quoteForShell(script_path.string()) + " --input " +
      quoteForShell(output_dir) + " --output " +
      quoteForShell(output_dir + "visualizations/tumor_growth_3d.mp4");

  spdlog::info("Running 3D tumor replay: {}", command);
  const int result = std::system(command.c_str());
  if (result == 0) {
    spdlog::info("3D tumor replay saved to: {}visualizations/tumor_growth_3d.mp4", output_dir);
  } else {
    spdlog::warn("3D tumor replay failed with code: {}", result);
  }
}

}  // namespace CellEvoX::core
  