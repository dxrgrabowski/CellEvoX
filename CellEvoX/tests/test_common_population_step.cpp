#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <random>
#include <utility>
#include <vector>

#include <tbb/global_control.h>

#include "systems/CommonPopulationStep.hpp"

namespace {

void insertCell(CellMap& cells, uint32_t id, uint32_t parent_id, float fitness = 1.0f) {
    Cell cell(id);
    cell.parent_id = parent_id;
    cell.fitness = fitness;
    REQUIRE(cells.insert({id, std::move(cell)}));
}

std::vector<std::pair<uint32_t, uint32_t>> deathPayload(
    const std::vector<CellEvoX::systems::CommonDeathEvent>& deaths) {
    std::vector<std::pair<uint32_t, uint32_t>> payload;
    for (const auto& death : deaths) {
        payload.push_back({death.id, death.parent_id});
    }
    return payload;
}

}  // namespace

TEST_CASE("CommonPopulationStep returns without RNG work for empty or zero-capacity populations", "[CommonPopulationStep]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    SimulationConfig config;
    config.tau_step = 1.0;
    config.env_capacity = 100;

    CellMap cells;
    insertCell(cells, 10, 3);
    insertCell(cells, 11, 3);

    Graveyard graveyard;
    size_t actual_population = 0;
    size_t total_deaths = 7;
    std::mt19937 rng(123);

    const auto result = CellEvoX::systems::applyCommonPopulationStep(
        cells, graveyard, config, {}, 0.0, actual_population, total_deaths, 6.0, rng);

    REQUIRE(result.births.empty());
    REQUIRE(result.deaths.empty());
    REQUIRE(actual_population == cells.size());
    REQUIRE(total_deaths == 7);
    REQUIRE(cells.size() == 2);
    REQUIRE(graveyard.empty());
}

TEST_CASE("CommonPopulationStep preserves cells when no event reaches tau", "[CommonPopulationStep][Determinism]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    SimulationConfig config;
    config.tau_step = 0.0;
    config.env_capacity = 3;

    CellMap cells;
    insertCell(cells, 0, 100, 1.0f);
    insertCell(cells, 1, 101, 1.0f);
    insertCell(cells, 2, 102, 1.0f);

    Graveyard graveyard;
    size_t actual_population = 3;
    size_t total_deaths = 0;
    std::mt19937 rng(321);

    const auto result = CellEvoX::systems::applyCommonPopulationStep(
        cells, graveyard, config, {}, 0.0, actual_population, total_deaths, 1.0, rng);

    REQUIRE(result.births.empty());
    REQUIRE(result.deaths.empty());
    REQUIRE(actual_population == 3);
    REQUIRE(total_deaths == 0);
    REQUIRE(cells.size() == 3);
    REQUIRE(graveyard.empty());
}

TEST_CASE("CommonPopulationStep records deterministic death-only events", "[CommonPopulationStep][Determinism]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    SimulationConfig config;
    config.tau_step = 1.0e9;
    config.env_capacity = 3;

    CellMap cells;
    insertCell(cells, 0, 100, 1.0f);
    insertCell(cells, 1, 101, 1.0f);
    insertCell(cells, 2, 102, 1.0f);

    Graveyard graveyard;
    size_t actual_population = 3;
    size_t total_deaths = 4;
    std::mt19937 rng(456);

    const auto result = CellEvoX::systems::applyCommonPopulationStep(
        cells, graveyard, config, {}, 0.0, actual_population, total_deaths, 8.75, rng);

    REQUIRE(result.births.empty());
    REQUIRE(deathPayload(result.deaths) ==
            std::vector<std::pair<uint32_t, uint32_t>>{{0, 100}, {1, 101}, {2, 102}});
    REQUIRE(actual_population == 0);
    REQUIRE(total_deaths == 7);
    REQUIRE(cells.empty());
    REQUIRE(graveyard.size() == 3);

    for (const auto& [id, parent_id] : deathPayload(result.deaths)) {
        Graveyard::const_accessor accessor;
        REQUIRE(graveyard.find(accessor, id));
        REQUIRE(accessor->second.first == parent_id);
        REQUIRE(accessor->second.second == Catch::Approx(8.75));
    }
}
