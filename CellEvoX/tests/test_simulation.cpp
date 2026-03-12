#include <catch2/catch_test_macros.hpp>

#include <filesystem>
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
