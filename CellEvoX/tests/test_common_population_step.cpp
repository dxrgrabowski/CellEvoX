#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include <tbb/global_control.h>

#include "systems/CommonPopulationStep.hpp"
#include "systems/MutationEffect.hpp"
#include "utils/DeterministicRng.hpp"

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
        population, config, {}, 0.0, 0.0, actual_population, total_deaths, 6.0);

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
        population, config, {}, 0.0, 0.0, actual_population, total_deaths, 1.0);

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
        population, config, {}, 0.0, 0.0, actual_population, total_deaths, 8.75);

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
    MutationType gamma_mutation{};
    gamma_mutation.probability = 1.0f;
    gamma_mutation.type_id = 7;
    gamma_mutation.is_driver = true;
    gamma_mutation.effect_mode = MutationEffectMode::Gamma;
    gamma_mutation.effect_mean = 0.5f;
    gamma_mutation.effect_std = 0.5f;
    gamma_mutation.effect_limit = 0.5f;
    const std::map<uint8_t, MutationType> mutations = {
        {7, gamma_mutation},
    };
    CellEvoX::systems::DensePopulation base(3), xyz(3);
    for (uint32_t id = 0; id < 3; ++id) {
        base.at(id).fitness = xyz.at(id).fitness = 1.0e20f;
        base.at(id).mutations = xyz.at(id).mutations = {{100 + id, 7, 0.0f}};
    }
    size_t base_count = 3, xyz_count = 3;
    size_t base_deaths = 0, xyz_deaths = 0;
    for (int step = 1; step <= 5; ++step) {
        const auto no_events = CellEvoX::systems::applyCommonPopulationStep<false>(
            base, config, mutations, 1.0, 0.0, base_count, base_deaths, step);
        const auto events = CellEvoX::systems::applyCommonPopulationStep(
            xyz, config, mutations, 1.0, 0.0, xyz_count, xyz_deaths, step);
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
            REQUIRE(base.at(birth.id).mutations.front().mutation_id >= 100);
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

TEST_CASE("CommonPopulationStep clamps gamma effects at the configured limit",
          "[CommonPopulationStep][Gamma][Determinism]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    const auto direction =
        GENERATE(MutationEffectDirection::Positive, MutationEffectDirection::Negative);
    constexpr float kLimit = 0.5f;
    constexpr uint32_t kCells = 32;

    SimulationConfig config;
    config.tau_step = 1.0;
    config.seed = 12345;  // pinned: `clamped > 0` below depends on the draws
    config.env_capacity = 1'000'000'000;

    MutationType gamma_mutation{};
    gamma_mutation.probability = 1.0f;
    gamma_mutation.type_id = 9;
    gamma_mutation.effect_mode = MutationEffectMode::Gamma;
    gamma_mutation.effect_mean = 0.5f;
    gamma_mutation.effect_std = 0.5f;  // shape 1: P(draw > limit) ~ 0.37 per event
    gamma_mutation.effect_direction = direction;
    gamma_mutation.effect_limit = kLimit;
    const std::map<uint8_t, MutationType> mutations = {{9, gamma_mutation}};

    CellEvoX::systems::DensePopulation population(kCells);
    for (uint32_t id = 0; id < kCells; ++id) {
        population.at(id).fitness = 10.0f;
    }
    size_t actual_population = kCells;
    size_t total_deaths = 0;

    const auto events = CellEvoX::systems::applyCommonPopulationStep(
        population, config, mutations, 1.0, 0.0, actual_population, total_deaths, 4.0);

    REQUIRE(events.births.size() == 2 * kCells);
    const float sign = direction == MutationEffectDirection::Positive ? 1.0f : -1.0f;
    size_t mutated = 0;
    size_t clamped = 0;
    for (const auto& birth : events.births) {
        const auto& cell = population.at(birth.id);
        if (cell.mutations.empty()) {
            // Only one daughter of each division carries the new mutation.
            REQUIRE(cell.fitness == 10.0f);
            continue;
        }
        ++mutated;
        REQUIRE(cell.mutations.size() == 1);
        const float effect = cell.mutations.front().effect;
        REQUIRE(effect * sign > 0.0f);
        REQUIRE(std::abs(effect) <= kLimit);
        if (std::abs(effect) == kLimit) ++clamped;
        REQUIRE(cell.fitness == Catch::Approx(10.0f * (1.0 + effect)));
        REQUIRE(cell.fitness > 0.0f);
    }
    REQUIRE(mutated == kCells);
    REQUIRE(clamped > 0);
}

TEST_CASE("CommonPopulationStep applies Bernoulli and Poisson mutations in one division",
          "[CommonPopulationStep][Hybrid][Determinism]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    constexpr uint32_t kCells = 16;
    constexpr float kBaseFitness = 1.0e20f;
    constexpr uint8_t kBernoulliType = 3;
    constexpr uint8_t kPoissonType = 5;
    constexpr double kBernoulliProbability = 1.0;
    constexpr double kPoissonRate = 2.0;
    constexpr double kBernoulliEffect = 0.1;
    constexpr double kPoissonEffect = 0.05;

    SimulationConfig config;
    config.tau_step = 1.0;
    config.seed = 20261120;
    config.env_capacity = 1'000'000'000;

    MutationType bernoulli_mutation{};
    bernoulli_mutation.probability = static_cast<float>(kBernoulliProbability);
    bernoulli_mutation.type_id = kBernoulliType;
    bernoulli_mutation.count_model = MutationCountModel::Bernoulli;
    bernoulli_mutation.effect = static_cast<float>(kBernoulliEffect);

    MutationType poisson_mutation{};
    poisson_mutation.type_id = kPoissonType;
    poisson_mutation.count_model = MutationCountModel::Poisson;
    poisson_mutation.lambda = static_cast<float>(kPoissonRate);
    poisson_mutation.effect = static_cast<float>(kPoissonEffect);

    const std::map<uint8_t, MutationType> mutations = {
        {kBernoulliType, bernoulli_mutation},
        {kPoissonType, poisson_mutation},
    };

    const auto step = [&](CellEvoX::systems::DensePopulation& target) {
        size_t actual_population = kCells;
        size_t total_deaths = 0;
        return CellEvoX::systems::applyCommonPopulationStep(
            target, config, mutations, kBernoulliProbability, kPoissonRate, actual_population,
            total_deaths, 6.0);
    };

    CellEvoX::systems::DensePopulation population(kCells), replay(kCells);
    for (uint32_t id = 0; id < kCells; ++id) {
        population.at(id).fitness = kBaseFitness;
        replay.at(id).fitness = kBaseFitness;
    }

    const auto events = step(population);
    REQUIRE(events.births.size() == 2 * kCells);

    size_t poisson_events = 0;
    for (const auto& birth : events.births) {
        const auto& cell = population.at(birth.id);
        if (cell.mutations.empty()) {
            // The second daughter keeps the parent genotype and fitness.
            REQUIRE(cell.fitness == kBaseFitness);
            continue;
        }

        // The Bernoulli mutation is always realized (p = 1) and always comes first.
        REQUIRE(cell.mutations.front().mutation_type == kBernoulliType);
        REQUIRE(cell.mutations.front().effect ==
                Catch::Approx(static_cast<float>(kBernoulliEffect)));

        double expected_fitness = static_cast<double>(kBaseFitness) * (1.0 + kBernoulliEffect);
        for (size_t i = 1; i < cell.mutations.size(); ++i) {
            REQUIRE(cell.mutations[i].mutation_type == kPoissonType);
            REQUIRE(cell.mutations[i].effect ==
                    Catch::Approx(static_cast<float>(kPoissonEffect)));
            expected_fitness *= 1.0 + kPoissonEffect;
        }
        REQUIRE(cell.fitness == Catch::Approx(expected_fitness).epsilon(1e-6));
        REQUIRE(cell.fitness > 0.0f);

        // Every mutation from one division is stamped with the daughter's new id.
        for (const auto& mutation : cell.mutations) {
            REQUIRE(mutation.mutation_id == birth.id);
        }
        poisson_events += cell.mutations.size() - 1;
    }
    // With lambda = 2 over 16 divisions the Poisson pool must fire.
    REQUIRE(poisson_events > 0);

    // Same seed, step, and cell ids reproduce the same event payload.
    const auto replay_events = step(replay);
    REQUIRE(replay_events.births.size() == events.births.size());
    for (size_t i = 0; i < events.births.size(); ++i) {
        REQUIRE(replay_events.births[i].id == events.births[i].id);
        const auto& lhs = population.at(events.births[i].id);
        const auto& rhs = replay.at(replay_events.births[i].id);
        REQUIRE(lhs.fitness == rhs.fitness);
        REQUIRE(lhs.mutations == rhs.mutations);
    }
}

TEST_CASE("CommonPopulationStep keeps the Bernoulli mutation in gamma effect slot zero",
          "[CommonPopulationStep][Hybrid][Gamma][Determinism]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    constexpr uint32_t kCells = 32;
    constexpr float kBaseFitness = 10.0f;
    constexpr uint8_t kBernoulliType = 2;
    constexpr uint8_t kPoissonType = 6;
    constexpr double kPoissonRate = 3.0;
    constexpr float kConstantEffect = 0.02f;

    SimulationConfig config;
    config.tau_step = 1.0;
    config.seed = 777;
    config.env_capacity = 1'000'000'000;

    MutationType bernoulli_mutation{};
    bernoulli_mutation.probability = 1.0f;
    bernoulli_mutation.type_id = kBernoulliType;
    bernoulli_mutation.count_model = MutationCountModel::Bernoulli;
    bernoulli_mutation.effect = kConstantEffect;

    MutationType gamma_mutation{};
    gamma_mutation.type_id = kPoissonType;
    gamma_mutation.count_model = MutationCountModel::Poisson;
    gamma_mutation.lambda = static_cast<float>(kPoissonRate);
    gamma_mutation.effect_mode = MutationEffectMode::Gamma;
    gamma_mutation.effect_mean = 0.1f;
    gamma_mutation.effect_std = 0.05f;
    gamma_mutation.effect_limit = 0.4f;

    const std::map<uint8_t, MutationType> mutations = {
        {kBernoulliType, bernoulli_mutation},
        {kPoissonType, gamma_mutation},
    };

    CellEvoX::systems::DensePopulation population(kCells);
    for (uint32_t id = 0; id < kCells; ++id) {
        population.at(id).fitness = kBaseFitness;
    }
    size_t actual_population = kCells;
    size_t total_deaths = 0;

    const double tau = 3.0;
    const auto events = CellEvoX::systems::applyCommonPopulationStep(
        population, config, mutations, 1.0, kPoissonRate, actual_population, total_deaths, tau);
    REQUIRE(events.births.size() == 2 * kCells);

    const uint64_t rng_step = static_cast<uint64_t>(std::llround(tau / config.tau_step));
    size_t multi_poisson_daughters = 0;
    for (const auto& birth : events.births) {
        const auto& cell = population.at(birth.id);
        if (cell.mutations.empty()) {
            continue;
        }
        REQUIRE(cell.mutations.front().mutation_type == kBernoulliType);
        REQUIRE(cell.mutations.front().effect == kConstantEffect);
        if (cell.mutations.size() > 2) {
            ++multi_poisson_daughters;
        }
        for (size_t slot = 1; slot < cell.mutations.size(); ++slot) {
            // The Bernoulli mutation consumed slot 0, so Poisson effects draw
            // from their position in the realized event list.
            const float expected = CellEvoX::systems::detail::sampleGammaMutationEffect(
                gamma_mutation, config.seed, rng_step, birth.parent_id,
                static_cast<uint32_t>(slot));
            REQUIRE(cell.mutations[slot].mutation_type == kPoissonType);
            REQUIRE(cell.mutations[slot].effect == expected);
        }
    }
    REQUIRE(multi_poisson_daughters > 0);
}

TEST_CASE("Deterministic poisson draws are reproducible across both sampling regimes",
          "[DeterministicRng][Poisson][Determinism]") {
    using CellEvoX::deterministic_rng::poisson01;

    REQUIRE(poisson01(7, 3, 11, 0, 0.0) == 0);
    REQUIRE(poisson01(7, 3, 11, 0, -1.0) == 0);

    const auto sample_mean = [](double lambda, size_t samples) {
        double total = 0.0;
        for (size_t i = 0; i < samples; ++i) {
            const uint64_t first = poisson01(123, 5, i, 0, lambda);
            REQUIRE(first == poisson01(123, 5, i, 0, lambda));
            total += static_cast<double>(first);
        }
        return total / static_cast<double>(samples);
    };

    // Inversion for lambda < 10 and PTRS above it; both must match the mean.
    REQUIRE(sample_mean(3.0, 4000) == Catch::Approx(3.0).margin(0.3));
    REQUIRE(sample_mean(25.0, 4000) == Catch::Approx(25.0).margin(1.0));
}
