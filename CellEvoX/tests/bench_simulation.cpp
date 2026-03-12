#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>

#include <filesystem>
#include "systems/SimulationEngine.hpp"
#include "utils/SimulationConfig.hpp"

// Helper: builds a minimal config with no overhead (no stat/popul snapshots, no graveyard pruning)
static std::shared_ptr<SimulationConfig> makeConfig(
    size_t population,
    size_t env_capacity,
    double tau_step,
    const std::vector<MutationType>& mutations = {}
) {
    auto c = std::make_shared<SimulationConfig>();
    c->sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
    c->tau_step = tau_step;
    c->initial_population = population;
    c->env_capacity = env_capacity;
    // High snapshot resolutions to avoid stat/popul-snapshot overhead polluting benchmark
    c->stat_res = 100000;
    c->popul_res = 100000;
    c->graveyard_pruning_interval = 0;  // No pruning overhead
    c->output_path = "/tmp/test_bench_sim";
    c->mutations = mutations;
    return c;
}

// ============================================================
// 1. Population scaling — isolate raw stochasticStep throughput
//    One step per bench isolates a single stochasticStep call.
// ============================================================
TEST_CASE("stochasticStep: Population Scaling", "[stochasticStep][benchmark][scaling]") {
    std::filesystem::create_directories("/tmp/test_bench_sim/statistics");

    BENCHMARK("stochasticStep N=100") {
        auto cfg = makeConfig(100, 100000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    BENCHMARK("stochasticStep N=500") {
        auto cfg = makeConfig(500, 100000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    BENCHMARK("stochasticStep N=1000") {
        auto cfg = makeConfig(1000, 100000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    BENCHMARK("stochasticStep N=5000") {
        auto cfg = makeConfig(5000, 100000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    BENCHMARK("stochasticStep N=10000") {
        auto cfg = makeConfig(10000, 100000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    BENCHMARK("stochasticStep N=50000") {
        auto cfg = makeConfig(50000, 100000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };
}

// ============================================================
// 2. Mutation rate effect on stochasticStep
//    Mutations trigger branching + Cell copy, so rates matter.
// ============================================================
TEST_CASE("stochasticStep: Mutation Rate Impact", "[stochasticStep][benchmark][mutations]") {
    std::filesystem::create_directories("/tmp/test_bench_sim/statistics");

    // No mutations baseline
    BENCHMARK("stochasticStep N=5000 no mutations") {
        auto cfg = makeConfig(5000, 100000, 0.005, {});
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    // Low mutation pressure: 1 type, probability=0.01
    BENCHMARK("stochasticStep N=5000 low mutation (p=0.01)") {
        std::vector<MutationType> muts = {{ 0.05, 0.01, 1, false }};
        auto cfg = makeConfig(5000, 100000, 0.005, muts);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    // High mutation pressure: 1 type, probability=0.5
    BENCHMARK("stochasticStep N=5000 high mutation (p=0.5)") {
        std::vector<MutationType> muts = {{ 0.05, 0.5, 1, false }};
        auto cfg = makeConfig(5000, 100000, 0.005, muts);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    // Many mutation types (simulates future dimensionality extension)
    BENCHMARK("stochasticStep N=5000 many mutation types (8 types, p=0.05 each)") {
        std::vector<MutationType> muts;
        for (uint8_t i = 0; i < 8; ++i)
            muts.push_back({ 0.05 * (i + 1), 0.05, i, (i % 2 == 0) });
        auto cfg = makeConfig(5000, 100000, 0.005, muts);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };
}

// ============================================================
// 3. Environmental pressure (death scaling factor = N/Nc)
//    High pressure (N ≈ Nc) → many deaths → more graveyard ops
// ============================================================
TEST_CASE("stochasticStep: Environmental Pressure", "[stochasticStep][benchmark][pressure]") {
    std::filesystem::create_directories("/tmp/test_bench_sim/statistics");

    // Low pressure: N << Nc (cells mostly survive and replicate)
    BENCHMARK("stochasticStep N=1000 low pressure (Nc=100000)") {
        auto cfg = makeConfig(1000, 100000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    // Medium pressure: N ~ Nc/5
    BENCHMARK("stochasticStep N=1000 medium pressure (Nc=5000)") {
        auto cfg = makeConfig(1000, 5000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    // High pressure: N ≈ Nc (max death probability)
    BENCHMARK("stochasticStep N=1000 high pressure (Nc=1000)") {
        auto cfg = makeConfig(1000, 1000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    // Overcrowded: N > Nc (extreme death pressure)
    BENCHMARK("stochasticStep N=2000 overcrowded (Nc=1000)") {
        auto cfg = makeConfig(2000, 1000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };
}

// ============================================================
// 4. tau_step sensitivity
//    Larger tau_step → more birth/death events per step
//    (probability threshold changes relative to tau_step)
// ============================================================
TEST_CASE("stochasticStep: tau_step Sensitivity", "[stochasticStep][benchmark][tau]") {
    std::filesystem::create_directories("/tmp/test_bench_sim/statistics");

    BENCHMARK("stochasticStep N=5000 tau=0.001 (very fine)") {
        auto cfg = makeConfig(5000, 100000, 0.001);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    BENCHMARK("stochasticStep N=5000 tau=0.005") {
        auto cfg = makeConfig(5000, 100000, 0.005);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    BENCHMARK("stochasticStep N=5000 tau=0.05") {
        auto cfg = makeConfig(5000, 100000, 0.05);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };

    BENCHMARK("stochasticStep N=5000 tau=0.5 (coarse)") {
        auto cfg = makeConfig(5000, 100000, 0.5);
        SimulationEngine eng(cfg);
        eng.run(1);
        return 0;
    };
}

// ============================================================
// 5. Multi-step simulation scalability (regression baseline)
//    This is the key regression test: track run() wall time as
//    code changes, since stochasticStep is called `steps` times.
// ============================================================
TEST_CASE("SimulationEngine run() Scalability", "[benchmark][regression]") {
    std::filesystem::create_directories("/tmp/test_bench_sim/statistics");

    BENCHMARK("run() N=1000 x10 steps (baseline)") {
        auto cfg = makeConfig(1000, 10000, 0.005);
        SimulationEngine eng(cfg);
        return eng.run(10).cells.size();
    };

    BENCHMARK("run() N=1000 x100 steps") {
        auto cfg = makeConfig(1000, 10000, 0.005);
        SimulationEngine eng(cfg);
        return eng.run(100).cells.size();
    };

    BENCHMARK("run() N=5000 x10 steps") {
        auto cfg = makeConfig(5000, 50000, 0.005);
        SimulationEngine eng(cfg);
        return eng.run(10).cells.size();
    };

    BENCHMARK("run() N=10000 x5 steps") {
        auto cfg = makeConfig(10000, 100000, 0.005);
        SimulationEngine eng(cfg);
        return eng.run(5).cells.size();
    };
}
