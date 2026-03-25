#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>

#include <filesystem>
#include "systems/SimulationEngine.hpp"
#include "utils/SimulationConfig.hpp"

// Helper: minimal overhead config for 3D spatial ABM.
static std::shared_ptr<SimulationConfig> makeConfig(
    size_t population,
    size_t env_capacity,
    const std::vector<MutationType>& mutations = {}
) {
    auto c = std::make_shared<SimulationConfig>();
    c->sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
    c->tau_step = 0.005;
    c->initial_population = population;
    c->env_capacity = env_capacity;
    c->stat_res = 1000000;
    c->popul_res = 1000000;
    c->graveyard_pruning_interval = 0;
    c->output_path = "/tmp/test_bench_sim";
    c->mutations = mutations;
    c->verbosity = 0;
    // Spatial ABM params — reduce mech iterations for benchmark speed
    c->mech_iterations = 3;
    c->mech_dt = 0.1f;
    c->max_local_density = 10.0f;
    c->sample_radius = 3.0f;
    c->spawn_offset = 0.1f;
    return c;
}

// ============================================================
// Consolidated High-N Regression Suite (N=100,000)
//
// These benchmarks now exercise the full 3D spatial ABM code path
// including SpatialHashGrid, local density, and mechanical relaxation.
// ============================================================

TEST_CASE("Simulation: High-Performance Regression", "[benchmark]") {
    std::filesystem::create_directories("/tmp/test_bench_sim/statistics");
    std::filesystem::create_directories("/tmp/test_bench_sim/population_data");

    // 1. RAW STEP (Minimum logic)
    // Measures: TBB scheduling, grid build, force calc, birth/death compute.
    BENCHMARK("stochasticStep N=100000 [baseline]") {
        auto cfg = makeConfig(100000, 10000000); // Low death pressure
        SimulationEngine eng(cfg);
        return eng.run(1); 
    };

    // 2. MUTATION STRESS (Logic branching)
    // Measures: Mutation probability checks and cell-copy overhead.
    BENCHMARK("stochasticStep N=100000 [mutations: 8 types]") {
        std::vector<MutationType> muts;
        for (uint8_t i = 0; i < 8; ++i)
            muts.push_back({ 0.05f * (i + 1), 0.05f, i, (i % 2 == 0) });
        auto cfg = makeConfig(100000, 10000000, muts);
        SimulationEngine eng(cfg);
        return eng.run(1);
    };

    // 3. PRESSURE STRESS (High local density)
    // Measures: Death scaling with local density and force calculations.
    BENCHMARK("stochasticStep N=100000 [pressure: high]") {
        auto cfg = makeConfig(100000, 100000); // N == Nc (Max death pressure)
        SimulationEngine eng(cfg);
        return eng.run(1);
    };

    // 4. MULTI-STEP STABILITY (Cumulative)
    // Measures: Grid rebuild cost over multiple steps.
    BENCHMARK("run() N=50000 x5 steps [stability]") {
        auto cfg = makeConfig(50000, 1000000);
        SimulationEngine eng(cfg);
        return eng.run(5);
    };
}
