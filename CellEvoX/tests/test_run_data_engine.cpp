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
#include "systems/SimulationEngine.hpp"

namespace {

std::filesystem::path testTempPath(std::string_view name) {
    return std::filesystem::temp_directory_path() / "cellevox_tests" / name;
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
