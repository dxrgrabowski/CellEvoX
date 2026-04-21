#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>

#include <cstdint>
#include <filesystem>

#include "io/PopulationSnapshotIO.hpp"
#include "systems/SimulationEngine.hpp"
#include "systems/SimulationEngine3D.hpp"
#include "utils/SimulationConfig.hpp"

// Helper: minimal overhead config.
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
    return c;
}

static std::shared_ptr<SimulationConfig> makeSpatial3DConfig(
    size_t population,
    float domain_size,
    float max_local_density,
    int mech_substeps = 2
) {
    auto c = std::make_shared<SimulationConfig>();
    c->sim_type = SimulationType::SPATIAL_3D_ABM;
    c->tau_step = 0.005;
    c->initial_population = population;
    c->env_capacity = population * 8;
    c->stat_res = 1000000;
    c->popul_res = 1000000;
    c->graveyard_pruning_interval = 0;
    c->output_path = "/tmp/test_bench_sim_3d";
    c->verbosity = 0;
    c->spatial_domain_size = domain_size;
    c->sample_radius = 3.0f;
    c->max_local_density = max_local_density;
    c->spring_constant = 0.2f;
    c->mech_dt = 0.05f;
    c->mech_substeps = mech_substeps;
    c->epsilon = 0.1f;
    return c;
}

#pragma pack(push, 1)
struct BenchmarkMutationPayloadRecord {
    uint32_t mutation_id;
    uint8_t mutation_type;
};

struct BenchmarkExtendedPopulationSnapshotRecord {
    CellEvoX::io::PopulationSnapshotRecord base;
    uint32_t mutation_offset;
};
#pragma pack(pop)

static_assert(sizeof(BenchmarkMutationPayloadRecord) == 5);
static_assert(sizeof(BenchmarkExtendedPopulationSnapshotRecord) == 40);

struct SyntheticSnapshotInput {
    std::vector<Cell> cells;
    std::vector<float> pos_x;
    std::vector<float> pos_y;
    std::vector<float> pos_z;
    bool spatial = false;
};

static SyntheticSnapshotInput makeSyntheticSnapshotInput(
    size_t population,
    uint16_t mutations_per_cell,
    bool spatial
) {
    SyntheticSnapshotInput input;
    input.spatial = spatial;
    input.cells.reserve(population);
    if (spatial) {
        input.pos_x.reserve(population);
        input.pos_y.reserve(population);
        input.pos_z.reserve(population);
    }

    for (size_t i = 0; i < population; ++i) {
        Cell cell(static_cast<uint32_t>(i + 1));
        cell.parent_id = (i == 0) ? 0u : static_cast<uint32_t>((i / 2) + 1);
        cell.fitness = 1.0f + 0.01f * static_cast<float>(i % 7);
        cell.mutations.reserve(mutations_per_cell);
        for (uint16_t j = 0; j < mutations_per_cell; ++j) {
            cell.mutations.push_back(
                {static_cast<uint32_t>(1'000'000ull + i * mutations_per_cell + j),
                 static_cast<uint8_t>(j % 8)});
        }

        input.cells.push_back(std::move(cell));
        if (spatial) {
            input.pos_x.push_back(static_cast<float>(i % 128) * 0.5f);
            input.pos_y.push_back(static_cast<float>((i / 128) % 128) * 0.5f);
            input.pos_z.push_back(static_cast<float>((i / 16384) % 128) * 0.5f);
        }
    }

    return input;
}

static size_t totalMutationCount(const SyntheticSnapshotInput& input) {
    size_t total = 0;
    for (const auto& cell : input.cells) {
        total += cell.mutations.size();
    }
    return total;
}

static std::vector<CellEvoX::io::PopulationSnapshotRecord> buildCompactSnapshot(
    const SyntheticSnapshotInput& input
) {
    std::vector<CellEvoX::io::PopulationSnapshotRecord> snapshot;
    snapshot.reserve(input.cells.size());

    for (size_t i = 0; i < input.cells.size(); ++i) {
        const auto& cell = input.cells[i];
        snapshot.push_back({
            cell.id,
            cell.parent_id,
            cell.fitness,
            input.spatial ? input.pos_x[i] : std::numeric_limits<float>::quiet_NaN(),
            input.spatial ? input.pos_y[i] : std::numeric_limits<float>::quiet_NaN(),
            input.spatial ? input.pos_z[i] : std::numeric_limits<float>::quiet_NaN(),
            static_cast<uint16_t>(cell.mutations.size()),
            0,
            0,
            static_cast<uint8_t>(input.spatial ? 1 : 0),
            {0, 0, 0}
        });
    }

    return snapshot;
}

static std::pair<std::vector<BenchmarkExtendedPopulationSnapshotRecord>,
                 std::vector<BenchmarkMutationPayloadRecord>>
buildExtendedSnapshot(const SyntheticSnapshotInput& input) {
    std::vector<BenchmarkExtendedPopulationSnapshotRecord> snapshot;
    std::vector<BenchmarkMutationPayloadRecord> mutation_payload;

    snapshot.reserve(input.cells.size());
    mutation_payload.reserve(totalMutationCount(input));

    for (size_t i = 0; i < input.cells.size(); ++i) {
        const auto& cell = input.cells[i];
        const uint32_t mutation_offset = static_cast<uint32_t>(mutation_payload.size());
        for (const auto& [mutation_id, mutation_type] : cell.mutations) {
            mutation_payload.push_back({mutation_id, mutation_type});
        }

        snapshot.push_back({
            {
                cell.id,
                cell.parent_id,
                cell.fitness,
                input.spatial ? input.pos_x[i] : std::numeric_limits<float>::quiet_NaN(),
                input.spatial ? input.pos_y[i] : std::numeric_limits<float>::quiet_NaN(),
                input.spatial ? input.pos_z[i] : std::numeric_limits<float>::quiet_NaN(),
                static_cast<uint16_t>(cell.mutations.size()),
                0,
                0,
                static_cast<uint8_t>(input.spatial ? 1 : 0),
                {0, 0, 0}
            },
            mutation_offset
        });
    }

    return {std::move(snapshot), std::move(mutation_payload)};
}

static size_t compactSnapshotBytes(const SyntheticSnapshotInput& input) {
    return sizeof(CellEvoX::io::PopulationSnapshotFileHeader) +
           input.cells.size() * sizeof(CellEvoX::io::PopulationSnapshotRecord);
}

static size_t extendedSnapshotBytes(const SyntheticSnapshotInput& input) {
    return sizeof(CellEvoX::io::PopulationSnapshotFileHeader) +
           input.cells.size() * sizeof(BenchmarkExtendedPopulationSnapshotRecord) +
           totalMutationCount(input) * sizeof(BenchmarkMutationPayloadRecord);
}

// ============================================================
// Consolidated High-N Regression Suite (N=100,000)
//
// We only benchmark at N=100k because:
//   1. Inter-run variance (CV%) is lowest (~0.7%).
//   2. Sample count is high enough to be meaningful (~32ms/iter).
//   3. TBB/Allocation overhead is amortized over the step.
//
// These 4 benchmarks cover all critical code paths.
// ============================================================

TEST_CASE("Simulation: High-Performance Regression", "[benchmark]") {
    std::filesystem::create_directories("/tmp/test_bench_sim/statistics");
    std::filesystem::create_directories("/tmp/test_bench_sim_3d/statistics");

    // 1. RAW STEP (Minimum logic)
    // Measures: TBB scheduling and raw birth/death compute.
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
            muts.push_back({ 0.05f * static_cast<float>(i + 1), 0.05f, i, (i % 2 == 0) });
        auto cfg = makeConfig(100000, 10000000, muts);
        SimulationEngine eng(cfg);
        return eng.run(1);
    };

    // 3. PRESSURE STRESS (Graveyard overhead)
    // Measures: Death scaling (N/Nc) and high-frequency graveyard writes.
    BENCHMARK("stochasticStep N=100000 [pressure: high]") {
        auto cfg = makeConfig(100000, 100000); // N == Nc (Max death pressure)
        SimulationEngine eng(cfg);
        return eng.run(1);
    };

    // 4. MULTI-STEP STABILITY (Cumulative)
    // Measures: Snapshot scheduling and generational overhead.
    BENCHMARK("run() N=50000 x5 steps [stability]") {
        auto cfg = makeConfig(50000, 1000000);
        SimulationEngine eng(cfg);
        return eng.run(5);
    };
}

TEST_CASE("Simulation: 2D vs 3D Time Comparison", "[benchmark][timing-comparison]") {
    std::filesystem::create_directories("/tmp/test_bench_sim/statistics");
    std::filesystem::create_directories("/tmp/test_bench_sim_3d/statistics");

    // Balanced scenario for side-by-side timing at the engine level.
    BENCHMARK("2D run() N=20000 x3 steps [timing]") {
        auto cfg = makeConfig(20000, 400000);
        SimulationEngine eng(cfg);
        return eng.run(3);
    };

    BENCHMARK("3D run() N=20000 x3 steps [timing]") {
        auto cfg = makeSpatial3DConfig(20000, 64.0f, 8.0f, 2);
        SimulationEngine3D eng(cfg);
        return eng.run(3);
    };

    BENCHMARK("2D run() N=50000 x1 step [timing]") {
        auto cfg = makeConfig(50000, 1000000);
        SimulationEngine eng(cfg);
        return eng.run(1);
    };

    BENCHMARK("3D run() N=50000 x1 step [timing]") {
        auto cfg = makeSpatial3DConfig(50000, 88.0f, 8.0f, 2);
        SimulationEngine3D eng(cfg);
        return eng.run(1);
    };
}

TEST_CASE("Population snapshot serialization tradeoff", "[benchmark][snapshot-serialization]") {
    const auto non_spatial_input = makeSyntheticSnapshotInput(2000000, 4, false);
    const auto spatial_input = makeSyntheticSnapshotInput(2000000, 4, true);

    REQUIRE(extendedSnapshotBytes(non_spatial_input) > compactSnapshotBytes(non_spatial_input));
    REQUIRE(extendedSnapshotBytes(spatial_input) > compactSnapshotBytes(spatial_input));

    BENCHMARK("2D snapshot serialize N=2000000 mut=4 [compact]") {
        return buildCompactSnapshot(non_spatial_input);
    };

    BENCHMARK("2D snapshot serialize N=2000000 mut=4 [full-mutations]") {
        return buildExtendedSnapshot(non_spatial_input);
    };

    BENCHMARK("3D snapshot serialize N=2000000 mut=4 [compact]") {
        return buildCompactSnapshot(spatial_input);
    };

    BENCHMARK("3D snapshot serialize N=2000000 mut=4 [full-mutations]") {
        return buildExtendedSnapshot(spatial_input);
    };
}
