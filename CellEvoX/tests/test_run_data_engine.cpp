#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/RunDataEngine.hpp"
#include "io/PopulationSnapshotIO.hpp"
#include "systems/SimulationEngine.hpp"

namespace {

std::filesystem::path testTempPath(std::string_view name) {
    return std::filesystem::temp_directory_path() / "cellevox_tests" / name;
}

bool fileNonEmpty(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 0;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::ifstream file(path);
    REQUIRE(file.is_open());

    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST_CASE("RunDataEngine exports generational stats and phylogeny CSV files", "[RunDataEngine]") {
    const auto base_output_path = testTempPath("test_run_data_engine_export_to_csv");
    std::filesystem::remove_all(base_output_path);

    CellMap cells;
    Cell cell(1);
    cell.parent_id = 0;
    cell.fitness = 1.25f;
    REQUIRE(cells.insert({1, std::move(cell)}));

    Graveyard graveyard;
    std::vector<StatSnapshot> stats = {
        {0.5, 1.1, 0.2, 2.0, 0.5, 10, 0.01, 0.02, 0.03, 0.04},
        {1.0, 1.2, 0.3, 3.0, 0.6, 11, 0.11, 0.12, 0.13, 0.14},
    };

    auto run = std::make_shared<ecs::Run>(
        std::move(cells),
        std::map<uint8_t, MutationType>{},
        std::move(graveyard),
        std::move(stats),
        std::vector<std::pair<int, CellMap>>{},
        0,
        1.0);

    auto config = std::make_shared<SimulationConfig>();
    config->output_path = base_output_path.string();
    config->verbosity = 0;

    CellEvoX::core::RunDataEngine data_engine(config, run, "");
    data_engine.exportToCSV();

    const auto output_path = std::filesystem::path(config->output_path);
    REQUIRE(std::filesystem::exists(output_path / "statistics" / "generational_statistics.csv"));
    REQUIRE(std::filesystem::exists(output_path / "phylogeny" / "phylogenetic_tree.csv"));

    const auto stat_lines = readLines(output_path / "statistics" / "generational_statistics.csv");
    REQUIRE(stat_lines.size() == 3);
    REQUIRE(stat_lines[0] ==
            "Generation,TotalLivingCells,MeanFitness,FitnessVariance,FitnessSkewness,"
            "FitnessKurtosis,MeanMutations,MutationsVariance,MutationsSkewness,MutationsKurtosis");
    REQUIRE(stat_lines[1] == "0.5,10,1.1,0.2,0.01,0.02,2,0.5,0.03,0.04");
    REQUIRE(stat_lines[2] == "1,11,1.2,0.3,0.11,0.12,3,0.6,0.13,0.14");

    const auto tree_lines = readLines(output_path / "phylogeny" / "phylogenetic_tree.csv");
    REQUIRE_FALSE(tree_lines.empty());
    REQUIRE(tree_lines[0] == "NodeID,ParentID,ChildSum,DeathTime");
    REQUIRE(std::find(tree_lines.begin(), tree_lines.end(), "0,0,1,0") != tree_lines.end());
    REQUIRE(std::find(tree_lines.begin(), tree_lines.end(), "1,0,1,0") != tree_lines.end());
}

// Regression tests for the `--analyze` postprocessing gap: RunDataEngine(analyze_directory)
// attaches no live ecs::Run, so plotFitnessStatistics/plotMutationsStatistics/
// plotLivingCellsOverGenerations must reconstruct their input from
// statistics/generational_statistics.csv on disk instead of crashing on a null run or silently
// producing nothing. See CellEvoX::core::RunDataEngine::resolveGenerationalStats.

TEST_CASE("RunDataEngine reconstructs general_plots from CSV when no live run is attached",
          "[RunDataEngine][Analyze]") {
    const auto base_output_path = testTempPath("test_run_data_engine_analyze_general_plots");
    std::filesystem::remove_all(base_output_path);
    std::filesystem::create_directories(base_output_path / "statistics");
    std::filesystem::create_directories(base_output_path / "general_plots");

    {
        std::ofstream file(base_output_path / "statistics" / "generational_statistics.csv");
        REQUIRE(file.is_open());
        file << "Generation,TotalLivingCells,MeanFitness,FitnessVariance,FitnessSkewness,"
                "FitnessKurtosis,MeanMutations,MutationsVariance,MutationsSkewness,MutationsKurtosis\n";
        file << "0.5,10,1.1,0.2,0.01,0.02,2,0.5,0.03,0.04\n";
        file << "1,11,1.2,0.3,0.11,0.12,3,0.6,0.13,0.14\n";
    }

    // Mirrors `CellEvoX --analyze <dir>`: constructed from a directory path, no ecs::Run set.
    CellEvoX::core::RunDataEngine data_engine(base_output_path.string());

    data_engine.plotLivingCellsOverGenerations();
    data_engine.plotFitnessStatistics();
    data_engine.plotMutationsStatistics();

    const auto plots_dir = base_output_path / "general_plots";
    for (const char* filename : {
             "living_cells_over_generations.png",
             "mean_fitness_over_generations.png",
             "fitness_variance_over_generations.png",
             "fitness_skewness_over_generations.png",
             "fitness_kurtosis_over_generations.png",
             "mean_mutations_over_generations.png",
             "mutations_variance_over_generations.png",
             "mutations_skewness_over_generations.png",
             "mutations_kurtosis_over_generations.png",
         }) {
        INFO("Expected general_plots/" << filename << " to be created and non-empty");
        REQUIRE(fileNonEmpty(plots_dir / filename));
    }
}

TEST_CASE("RunDataEngine skips general_plots without crashing when no data is available",
          "[RunDataEngine][Analyze]") {
    const auto base_output_path =
        testTempPath("test_run_data_engine_analyze_general_plots_missing");
    std::filesystem::remove_all(base_output_path);
    std::filesystem::create_directories(base_output_path / "general_plots");
    // Deliberately no statistics/generational_statistics.csv and no live run attached.

    CellEvoX::core::RunDataEngine data_engine(base_output_path.string());

    // Previously these functions unconditionally dereferenced a (possibly null) `run` pointer.
    // They must now degrade gracefully instead of crashing when there is truly no data source.
    REQUIRE_NOTHROW(data_engine.plotLivingCellsOverGenerations());
    REQUIRE_NOTHROW(data_engine.plotFitnessStatistics());
    REQUIRE_NOTHROW(data_engine.plotMutationsStatistics());

    const auto plots_dir = base_output_path / "general_plots";
    REQUIRE_FALSE(std::filesystem::exists(plots_dir / "living_cells_over_generations.png"));
    REQUIRE_FALSE(std::filesystem::exists(plots_dir / "mean_fitness_over_generations.png"));
    REQUIRE_FALSE(std::filesystem::exists(plots_dir / "mean_mutations_over_generations.png"));
}

TEST_CASE(
    "RunDataEngine reconstructs mutation histograms and VAF diagrams from binary snapshots in "
    "analyze mode",
    "[RunDataEngine][Analyze]") {
    const auto base_output_path = testTempPath("test_run_data_engine_analyze_mutation_plots");
    std::filesystem::remove_all(base_output_path);
    std::filesystem::create_directories(base_output_path / "mutation_histograms");
    std::filesystem::create_directories(base_output_path / "vaf_diagrams");

    using CellEvoX::io::MutationPayloadKind;
    using CellEvoX::io::PopulationSnapshotDriverMutation;
    using CellEvoX::io::PopulationSnapshotRecord;

    const std::vector<PopulationSnapshotRecord> records = {
        {1, 0, 1.0f, 0.0f, 0.0f, 0.0f, 2, 1, 0, 0, {0, 0, 0}},
        {2, 0, 1.0f, 0.0f, 0.0f, 0.0f, 1, 1, 1, 0, {0, 0, 0}},
    };
    const std::vector<PopulationSnapshotDriverMutation> mutations = {
        {101, 1},
        {102, 1},
    };

    REQUIRE(CellEvoX::io::writePopulationSnapshot(
        base_output_path / "population_data" / "population_generation_500.bin",
        0.5,
        0,
        records,
        mutations,
        MutationPayloadKind::DriverOnly));

    CellEvoX::core::RunDataEngine data_engine(base_output_path.string());
    data_engine.plotMutationWave();
    data_engine.plotMutationFrequency();

    REQUIRE(fileNonEmpty(base_output_path / "mutation_histograms" /
                         "mutation_wave_histogram_generation_500.png"));
    REQUIRE(fileNonEmpty(base_output_path / "vaf_diagrams" / "vaf_histogram_generation_500.png"));
}
