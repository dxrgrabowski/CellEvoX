#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include "utils/SimulationConfig.hpp"
#include "systems/SimulationEngine.hpp"
#include "ecs/Cell.hpp"

// Namespace using removed

TEST_CASE("SimulationConfig parses correctly from default JSON", "[SimulationConfig]") {
    nlohmann::json j = {
        {"stochastic", true},
        {"tau_step", 0.005},
        {"initial_population", 100},
        {"env_capacity", 1000},
        {"steps", 20},
        {"statistics_resolution", 1},
        {"population_statistics_res", 5},
        {"output_path", "./output/"},
        {"mutations", nlohmann::json::array({
            {{"effect", 0.0}, {"probability", 0.1}, {"id", 1}, {"is_driver", false}},
            {{"effect", 0.1}, {"probability", 0.01}, {"id", 2}, {"is_driver", true}}
        })}
    };

    auto config = utils::fromJson(j);

    REQUIRE(config.sim_type == SimulationType::STOCHASTIC_TAU_LEAP);
    REQUIRE(config.tau_step == 0.005);
    REQUIRE(config.initial_population == 100);
    REQUIRE(config.env_capacity == 1000);
    REQUIRE(config.steps == 20);
    REQUIRE(config.stat_res == 1);
    REQUIRE(config.popul_res == 5);
    REQUIRE(config.output_path == "./output/");
    REQUIRE(config.mutations.size() == 2);
    REQUIRE(config.mutations[1].is_driver == true);
}

TEST_CASE("Cell Initialization and Inheritance", "[Cell]") {
    Cell parent(1);
    parent.fitness = 1.0;
    parent.mutations.push_back({0, 1}); // Parent has a mutation

    Cell child(parent, 1.25); // Create child from parent with higher fitness
    
    REQUIRE(child.parent_id == 1);
    REQUIRE(child.fitness == 1.25);
    REQUIRE(child.mutations.size() == 1);
    REQUIRE(child.mutations[0].second == 1); // Child inherits mutation type
    REQUIRE(child.id == 0); // ID hasn't been explicitly assigned yet
}

TEST_CASE("SimulationEngine Core Processing", "[SimulationEngine]") {
    auto config = std::make_shared<SimulationConfig>();
    config->sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
    config->tau_step = 1.0; // Set tau_step to 1.0 so each step increments tau by 1
    config->initial_population = 100;
    config->env_capacity = 10000;
    config->steps = 10;
    config->stat_res = 1;
    config->popul_res = 1;
    config->output_path = "/tmp/test_sim";

    // Create output dirs to prevent memory log warning
    std::filesystem::create_directories("/tmp/test_sim/statistics");

    SimulationEngine engine(config);
    auto runData = engine.run(10); // Runs 10 steps, tau goes from 1.0 to 10.0
    
    REQUIRE(runData.cells.size() > 0);
    // Because stat_res = 1 and tau hits 1, 2, ..., 10, it should snapshot 10 times.
    REQUIRE(runData.generational_stat_report.size() == 10);
    REQUIRE(runData.generational_popul_report.size() == 10);
}

TEST_CASE("Simulation Determinism", "[Determinism]") {
    auto config1 = std::make_shared<SimulationConfig>();
    config1->sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
    config1->tau_step = 0.05;
    config1->seed = 42;
    config1->initial_population = 100;
    config1->env_capacity = 1000;
    config1->steps = 100;
    config1->stat_res = 1;
    config1->popul_res = 10;
    config1->output_path = "/tmp/test_sim1";
    config1->mutations.push_back({0.1f, 0.05f, 1, false});

    auto config2 = std::make_shared<SimulationConfig>();
    *config2 = *config1; // exact copy
    config2->output_path = "/tmp/test_sim2";

    SimulationEngine engine1(config1);
    auto runData1 = engine1.run(100);

    SimulationEngine engine2(config2);
    auto runData2 = engine2.run(100);

    // Verify sizes
    REQUIRE(runData1.generational_stat_report.size() == runData2.generational_stat_report.size());
    
    // Verify values
    for(size_t i = 0; i < runData1.generational_stat_report.size(); ++i) {
        REQUIRE(runData1.generational_stat_report[i].mean_fitness == runData2.generational_stat_report[i].mean_fitness);
        REQUIRE(runData1.generational_stat_report[i].total_living_cells == runData2.generational_stat_report[i].total_living_cells);
    }
}

inline void require_approx(double actual, double expected, double margin = 1e-6) {
    if (std::isnan(actual) && std::isnan(expected)) return;
    REQUIRE(std::abs(actual - expected) <= margin);
}

TEST_CASE("Algorithmic Correctness Baseline", "[Correctness]") {
    auto config = std::make_shared<SimulationConfig>();
    config->sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
    config->tau_step = 0.05;
    config->seed = 42;
    config->initial_population = 100;
    config->env_capacity = 1000;
    config->steps = 100;
    config->stat_res = 1;
    config->popul_res = 10;
    config->output_path = "/tmp/test_sim_correctness";
    config->mutations.push_back({0.1f, 0.05f, 1, false});

    SimulationEngine engine(config);
    auto runData = engine.run(100);

    std::string baseline_path = "";
    std::filesystem::path current_dir = std::filesystem::current_path();
    while (current_dir != current_dir.parent_path()) {
        std::filesystem::path test_path = current_dir / "tests" / "benchmarks" / "correctness_baseline.json";
        if (std::filesystem::exists(test_path)) {
            baseline_path = test_path.string();
            break;
        }
        std::filesystem::path test_path_alt = current_dir / "CellEvoX" / "tests" / "benchmarks" / "correctness_baseline.json";
        if (std::filesystem::exists(test_path_alt)) {
            baseline_path = test_path_alt.string();
            break;
        }
        current_dir = current_dir.parent_path();
    }

    // Check if we should update the baseline
    if (const char* update_flag = std::getenv("CELLEVOX_UPDATE_BASELINE")) {
        if (std::string(update_flag) == "1") {
            if (baseline_path.empty()) baseline_path = "CellEvoX/tests/benchmarks/correctness_baseline.json";
            nlohmann::json j_array = nlohmann::json::array();
            for (const auto& stat : runData.generational_stat_report) {
                j_array.push_back({
                    {"tau", stat.tau},
                    {"mean_fitness", stat.mean_fitness},
                    {"fitness_variance", stat.fitness_variance},
                    {"mean_mutations", stat.mean_mutations},
                    {"mutations_variance", stat.mutations_variance},
                    {"total_living_cells", stat.total_living_cells},
                    {"fitness_skewness", stat.fitness_skewness},
                    {"fitness_kurtosis", stat.fitness_kurtosis},
                    {"mutations_skewness", stat.mutations_skewness},
                    {"mutations_kurtosis", stat.mutations_kurtosis}
                });
            }
            std::filesystem::create_directories(std::filesystem::path(baseline_path).parent_path());
            std::ofstream out(baseline_path);
            out << j_array.dump(2);
            SUCCEED("Correctness baseline generated successfully.");
            return;
        }
    }

    // Otherwise, compare against baseline
    REQUIRE(!baseline_path.empty());
    REQUIRE(std::filesystem::exists(baseline_path));
    std::ifstream in(baseline_path);
    nlohmann::json j_baseline;
    in >> j_baseline;

    REQUIRE(j_baseline.is_array());
    REQUIRE(j_baseline.size() == runData.generational_stat_report.size());

    for (size_t i = 0; i < runData.generational_stat_report.size(); ++i) {
        const auto& actual = runData.generational_stat_report[i];
        const auto& expected = j_baseline[i];

        require_approx(actual.tau, expected["tau"].get<double>());
        require_approx(actual.mean_fitness, expected["mean_fitness"].get<double>());
        require_approx(actual.fitness_variance, expected["fitness_variance"].get<double>());
        require_approx(actual.mean_mutations, expected["mean_mutations"].get<double>());
        require_approx(actual.mutations_variance, expected["mutations_variance"].get<double>());
        REQUIRE(actual.total_living_cells == expected["total_living_cells"].get<size_t>());
        require_approx(actual.fitness_skewness, expected["fitness_skewness"].get<double>());
        require_approx(actual.fitness_kurtosis, expected["fitness_kurtosis"].get<double>());
        require_approx(actual.mutations_skewness, expected["mutations_skewness"].get<double>());
        require_approx(actual.mutations_kurtosis, expected["mutations_kurtosis"].get<double>());
    }
}