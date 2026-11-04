#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <utility>
#include <vector>

#include <tbb/global_control.h>

#include "systems/CommonPopulationStep.hpp"

namespace {

std::vector<std::pair<uint32_t, uint32_t>> deathPayload(
    const std::vector<CellEvoX::systems::CommonDeathEvent>& deaths) {
    std::vector<std::pair<uint32_t, uint32_t>> payload;
    for (const auto& death : deaths) {
        payload.push_back({death.id, death.parent_id});
    }
    return payload;
}

}  // namespace

TEST_CASE("CommonPopulationStep preserves empty or zero-capacity populations", "[CommonPopulationStep]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    SimulationConfig config;
    config.tau_step = 1.0;
    const auto [initial_population, capacity] =
        GENERATE(std::make_pair(0u, 100u), std::make_pair(2u, 0u));
    config.env_capacity = capacity;
    CellEvoX::systems::DensePopulation population(initial_population);

    Graveyard graveyard;
    size_t actual_population = initial_population;
    size_t total_deaths = 7;

    const auto result = CellEvoX::systems::applyCommonPopulationStep(
        population, config, {}, 0.0, actual_population, total_deaths, 6.0);

    REQUIRE(result.births.empty());
    REQUIRE(result.deaths.empty());
    REQUIRE(actual_population == initial_population);
    REQUIRE(total_deaths == 7);
    REQUIRE(population.cells.size() == initial_population);
    population.materializeGraveyard(graveyard);
    REQUIRE(graveyard.empty());
}

TEST_CASE("CommonPopulationStep preserves cells when no event reaches tau", "[CommonPopulationStep][Determinism]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    SimulationConfig config;
    config.tau_step = 0.0;
    config.env_capacity = 3;

    CellEvoX::systems::DensePopulation population(3);
    for (uint32_t id = 0; id < 3; ++id) {
        population.at(id).parent_id = 100 + id;
    }

    Graveyard graveyard;
    size_t actual_population = 3;
    size_t total_deaths = 0;

    const auto result = CellEvoX::systems::applyCommonPopulationStep(
        population, config, {}, 0.0, actual_population, total_deaths, 1.0);

    REQUIRE(result.births.empty());
    REQUIRE(result.deaths.empty());
    REQUIRE(actual_population == 3);
    REQUIRE(total_deaths == 0);
    REQUIRE(population.cells.size() == 3);
    population.materializeGraveyard(graveyard);
    REQUIRE(graveyard.empty());
}

TEST_CASE("CommonPopulationStep records deterministic death-only events", "[CommonPopulationStep][Determinism]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    SimulationConfig config;
    config.tau_step = 1.0e9;
    config.env_capacity = 3;

    CellEvoX::systems::DensePopulation population(3);
    for (uint32_t id = 0; id < 3; ++id) {
        population.at(id).parent_id = 100 + id;
    }

    Graveyard graveyard;
    size_t actual_population = 3;
    size_t total_deaths = 0;

    const auto result = CellEvoX::systems::applyCommonPopulationStep(
        population, config, {}, 0.0, actual_population, total_deaths, 8.75);

    REQUIRE(result.births.empty());
    REQUIRE(deathPayload(result.deaths) ==
            std::vector<std::pair<uint32_t, uint32_t>>{{0, 100}, {1, 101}, {2, 102}});
    REQUIRE(actual_population == 0);
    REQUIRE(total_deaths == 3);
    population.materializeGraveyard(graveyard);
    REQUIRE(graveyard.size() == 3);
    for (uint32_t id = 0; id < 3; ++id) {
        REQUIRE_FALSE(population.isAlive(id));
    }

    for (const auto& [id, parent_id] : deathPayload(result.deaths)) {
        Graveyard::const_accessor accessor;
        REQUIRE(graveyard.find(accessor, id));
        REQUIRE(accessor->second.first == parent_id);
        REQUIRE(accessor->second.second == Catch::Approx(8.75));
    }
}

TEST_CASE("CommonPopulationStep reuses slots and optionally returns the same events",
          "[CommonPopulationStep][Determinism][Parallel]") {
    const size_t workers = GENERATE(1u, 4u);
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, workers);
    SimulationConfig config;
    config.tau_step = 1.0;
    config.env_capacity = 1'000'000'000;
    const std::map<uint8_t, MutationType> mutations = {
        {7, MutationType{0.0f, 1.0f, 7, true}},
    };
    CellEvoX::systems::DensePopulation base(3), xyz(3);
    for (uint32_t id = 0; id < 3; ++id) {
        base.at(id).fitness = xyz.at(id).fitness = 1.0e20f;
        base.at(id).mutations = xyz.at(id).mutations = {{100 + id, 7}};
    }
    size_t base_count = 3, xyz_count = 3;
    size_t base_deaths = 0, xyz_deaths = 0;
    for (int step = 1; step <= 5; ++step) {
        const auto no_events = CellEvoX::systems::applyCommonPopulationStep<false>(
            base, config, mutations, 1.0, base_count, base_deaths, step);
        const auto events = CellEvoX::systems::applyCommonPopulationStep(
            xyz, config, mutations, 1.0, xyz_count, xyz_deaths, step);
        REQUIRE(no_events.births.empty());
        REQUIRE(no_events.deaths.empty());
        REQUIRE(base_count == xyz_count);
        REQUIRE(base_deaths == xyz_deaths);
        REQUIRE(events.births.size() == base_count);
        REQUIRE(events.deaths.size() * 2 == events.births.size());
        // All parents die in each division, and their slots are reused.
        REQUIRE(base.cells.size() == base_count);
        REQUIRE(xyz.cells.size() == xyz_count);
        REQUIRE(base.free_slots.empty());
        for (const auto& death : events.deaths) {
            REQUIRE_FALSE(xyz.isAlive(death.id));
        }
        for (const auto& birth : events.births) {
            REQUIRE(base.isAlive(birth.id));
            REQUIRE(xyz.isAlive(birth.id));
            REQUIRE(base.at(birth.id).parent_id == birth.parent_id);
            REQUIRE(base.at(birth.id).mutations == xyz.at(birth.id).mutations);
            REQUIRE(base.at(birth.id).fitness == xyz.at(birth.id).fitness);
            REQUIRE(base.at(birth.id).mutations.front().first >= 100);
        }
    }
    Graveyard base_graveyard, xyz_graveyard;
    base.materializeGraveyard(base_graveyard);
    xyz.materializeGraveyard(xyz_graveyard);
    REQUIRE(base_graveyard.size() == xyz_graveyard.size());
    REQUIRE(base.pending_graveyard_entries.empty());
    for (const auto& [id, entry] : base_graveyard) {
        Graveyard::const_accessor accessor;
        REQUIRE(xyz_graveyard.find(accessor, id));
        REQUIRE(accessor->second == entry);
    }
}
