#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <tbb/global_control.h>
#include "io/PopulationSnapshotIO.hpp"
#include "utils/SimulationConfig.hpp"
#include "spatial/SpatialHashGrid.hpp"
#include "systems/SimulationEngine.hpp"
#include "systems/SimulationEngine3D.hpp"
#include "ecs/Cell.hpp"

// Namespace using removed

inline std::vector<CellEvoX::io::PopulationSnapshotRecord> read_population_snapshot(
    const std::filesystem::path& path
);

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

TEST_CASE("SimulationConfig parses spatial 3D mode", "[SimulationConfig][Spatial3D]") {
    nlohmann::json j = {
        {"stochastic", true},
        {"simulation_mode", "spatial_3d"},
        {"tau_step", 0.05},
        {"initial_population", 32},
        {"env_capacity", 1000},
        {"steps", 10},
        {"statistics_resolution", 1},
        {"population_statistics_res", 2},
        {"output_path", "./output/"},
        {"spatial_domain_size", 256.0},
        {"max_local_density", 12.0},
        {"sample_radius", 4.0},
        {"spring_constant", 0.75},
        {"mech_dt", 0.2},
        {"mech_substeps", 7},
        {"epsilon", 0.15},
        {"mutations", nlohmann::json::array()}
    };

    auto config = utils::fromJson(j);

    REQUIRE(config.sim_type == SimulationType::SPATIAL_3D_ABM);
    REQUIRE(config.spatial_domain_size == Catch::Approx(256.0));
    REQUIRE(config.max_local_density == Catch::Approx(12.0));
    REQUIRE(config.sample_radius == Catch::Approx(4.0));
    REQUIRE(config.spring_constant == Catch::Approx(0.75));
    REQUIRE(config.mech_dt == Catch::Approx(0.2));
    REQUIRE(config.mech_substeps == 7);
    REQUIRE(config.epsilon == Catch::Approx(0.15));
}

TEST_CASE("SpatialHashGrid returns neighbors from nearby voxels", "[SpatialHashGrid]") {
    SpatialHashGrid grid(2.0f, 20.0f);
    std::vector<uint32_t> ids = {10, 11, 12};
    std::vector<float> px = {1.0f, 2.2f, 10.0f};
    std::vector<float> py = {1.0f, 1.1f, 10.0f};
    std::vector<float> pz = {1.0f, 0.9f, 10.0f};

    grid.rebuild(ids, px, py, pz);

    std::vector<uint32_t> neighbors;
    grid.queryRadius(1.0f, 1.0f, 1.0f, 2.0f, [&](uint32_t id) {
        neighbors.push_back(id);
    });

    std::sort(neighbors.begin(), neighbors.end());
    REQUIRE(neighbors == std::vector<uint32_t>{10, 11});
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
    const auto snapshot_path =
        std::filesystem::path(config->output_path) / "population_data" / "population_generation_1.bin";
    REQUIRE(std::filesystem::exists(snapshot_path));
    const auto snapshot = read_population_snapshot(snapshot_path);
    REQUIRE_FALSE(snapshot.empty());
    REQUIRE(std::all_of(snapshot.begin(), snapshot.end(), [](const auto& cell) {
        return cell.position_valid == 0;
    }));
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

inline std::vector<char> read_binary_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());

    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    REQUIRE(size >= 0);
    in.seekg(0, std::ios::beg);

    std::vector<char> bytes(static_cast<size_t>(size));
    if (size > 0) {
        in.read(bytes.data(), size);
        const bool read_completed = in.good() || in.eof();
        REQUIRE(read_completed);
    }
    return bytes;
}

inline std::vector<CellEvoX::io::PopulationSnapshotRecord> read_population_snapshot(
    const std::filesystem::path& path
) {
    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> snapshot;
    REQUIRE(CellEvoX::io::readPopulationSnapshot(path, header, snapshot));
    return snapshot;
}

inline double compute_mean_local_neighbors(
    const std::vector<CellEvoX::io::PopulationSnapshotRecord>& snapshot,
    float radius
) {
    if (snapshot.empty()) {
        return 0.0;
    }

    const float radius_sq = radius * radius;
    size_t total_neighbors = 0;
    for (size_t i = 0; i < snapshot.size(); ++i) {
        size_t local_neighbors = 0;
        for (size_t j = 0; j < snapshot.size(); ++j) {
            if (i == j) {
                continue;
            }

            const float dx = snapshot[i].x - snapshot[j].x;
            const float dy = snapshot[i].y - snapshot[j].y;
            const float dz = snapshot[i].z - snapshot[j].z;
            const float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq <= radius_sq) {
                ++local_neighbors;
            }
        }
        total_neighbors += local_neighbors;
    }

    return static_cast<double>(total_neighbors) / static_cast<double>(snapshot.size());
}

TEST_CASE("PopulationSnapshotIO preserves driver-only payload", "[PopulationSnapshotIO]") {
    const std::filesystem::path snapshot_path = "/tmp/test_population_snapshot_driver_payload.bin";
    std::filesystem::remove(snapshot_path);

    const std::vector<CellEvoX::io::PopulationSnapshotRecord> records = {
        {1, 0, 1.0f, 1.0f, 2.0f, 3.0f, 3, 1, 0, 1, {0, 0, 0}},
        {2, 1, 1.1f, 4.0f, 5.0f, 6.0f, 2, 2, 1, 1, {0, 0, 0}}
    };
    const std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> driver_mutations = {
        {101, 1},
        {202, 1},
        {303, 2}
    };

    REQUIRE(CellEvoX::io::writePopulationSnapshot(
        snapshot_path, 12.5, 3, records, driver_mutations));

    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> loaded_records;
    std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> loaded_driver_mutations;
    REQUIRE(CellEvoX::io::readPopulationSnapshot(
        snapshot_path, header, loaded_records, loaded_driver_mutations));

    REQUIRE(header.version == CellEvoX::io::kPopulationSnapshotVersion);
    REQUIRE(CellEvoX::io::hasDriverMutationPayload(header));
    REQUIRE(header.driver_mutation_count == driver_mutations.size());
    REQUIRE(loaded_records.size() == records.size());
    REQUIRE(loaded_driver_mutations.size() == driver_mutations.size());

    REQUIRE(loaded_records[0].mutations_count == 3);
    REQUIRE(loaded_records[0].driver_mutation_count == 1);
    REQUIRE(loaded_records[0].driver_mutation_offset == 0);
    REQUIRE(loaded_records[1].driver_mutation_count == 2);
    REQUIRE(loaded_records[1].driver_mutation_offset == 1);
    REQUIRE(loaded_driver_mutations[0].mutation_id == 101);
    REQUIRE(loaded_driver_mutations[1].mutation_id == 202);
    REQUIRE(loaded_driver_mutations[2].mutation_type == 2);
}

inline bool snapshot_within_domain(
    const std::vector<CellEvoX::io::PopulationSnapshotRecord>& snapshot,
    float domain_size
) {
    return std::all_of(snapshot.begin(), snapshot.end(), [&](const CellEvoX::io::PopulationSnapshotRecord& cell) {
        return cell.x >= 0.0f && cell.x <= domain_size &&
               cell.y >= 0.0f && cell.y <= domain_size &&
               cell.z >= 0.0f && cell.z <= domain_size;
    });
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
#ifdef CELLEVOX_SOURCE_DIR
    std::filesystem::path source_dir(CELLEVOX_SOURCE_DIR);
    std::filesystem::path macro_path = source_dir / "tests" / "benchmarks" / "correctness_baseline.json";
    baseline_path = macro_path.string();
#endif

    if (baseline_path.empty() || !std::filesystem::exists(std::filesystem::path(baseline_path).parent_path())) {
        std::filesystem::path current_dir = std::filesystem::current_path();
        while (current_dir != current_dir.parent_path()) {
            std::filesystem::path test_path = current_dir / "tests" / "benchmarks" / "correctness_baseline.json";
            if (std::filesystem::exists(test_path.parent_path())) {
                baseline_path = test_path.string();
                break;
            }
            std::filesystem::path test_path_alt = current_dir / "CellEvoX" / "tests" / "benchmarks" / "correctness_baseline.json";
            if (std::filesystem::exists(test_path_alt.parent_path())) {
                baseline_path = test_path_alt.string();
                break;
            }
            current_dir = current_dir.parent_path();
        }
    }

    // Check if we should update the baseline
    if (const char* update_flag = std::getenv("CELLEVOX_UPDATE_BASELINE")) {
        if (std::string(update_flag) == "1") {
            if (baseline_path.empty()) baseline_path = "tests/benchmarks/correctness_baseline.json";
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

TEST_CASE("SimulationEngine3D produces binary population snapshots", "[SimulationEngine3D]") {
    auto config = std::make_shared<SimulationConfig>();
    config->sim_type = SimulationType::SPATIAL_3D_ABM;
    config->tau_step = 1.0;
    config->seed = 7;
    config->initial_population = 16;
    config->env_capacity = 256;
    config->steps = 2;
    config->stat_res = 1;
    config->popul_res = 1;
    config->output_path = "/tmp/test_sim_3d";
    config->sample_radius = 3.0f;
    config->max_local_density = 8.0f;
    config->spring_constant = 0.1f;
    config->mech_dt = 0.05f;
    config->mech_substeps = 2;
    config->epsilon = 0.1f;

    std::filesystem::remove_all(config->output_path);
    std::filesystem::create_directories(config->output_path);

    SimulationEngine3D engine(config);
    auto runData = engine.run(2);

    REQUIRE(runData.generational_stat_report.size() == 2);
    REQUIRE(std::filesystem::exists("/tmp/test_sim_3d/population_data/population_generation_1.bin"));
    REQUIRE(std::filesystem::exists("/tmp/test_sim_3d/population_data/population_generation_2.bin"));
}

TEST_CASE("SimulationEngine3D grows from a sparse neutral state", "[SimulationEngine3D]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    auto config = std::make_shared<SimulationConfig>();
    config->sim_type = SimulationType::SPATIAL_3D_ABM;
    config->tau_step = 0.05;
    config->seed = 42;
    config->initial_population = 64;
    config->env_capacity = 512;
    config->steps = 80;
    config->stat_res = 1;
    config->popul_res = 10;
    config->output_path = "/tmp/test_sim_3d_sparse";
    config->spatial_domain_size = 32.0f;
    config->sample_radius = 3.0f;
    config->max_local_density = 8.0f;
    config->spring_constant = 0.2f;
    config->mech_dt = 0.05f;
    config->mech_substeps = 2;
    config->epsilon = 0.1f;
    config->verbosity = 0;

    std::filesystem::remove_all(config->output_path);
    std::filesystem::create_directories(config->output_path);

    SimulationEngine3D engine(config);
    auto runData = engine.run(80);

    REQUIRE_FALSE(runData.generational_stat_report.empty());
    REQUIRE(runData.generational_stat_report.back().total_living_cells > config->initial_population);
}

TEST_CASE("SimulationEngine3D deterministic smoke test", "[SimulationEngine3D][Determinism][CI]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    auto make_config = [](const std::string& output_path) {
        auto config = std::make_shared<SimulationConfig>();
        config->sim_type = SimulationType::SPATIAL_3D_ABM;
        config->tau_step = 1.0;
        config->seed = 321;
        config->initial_population = 16;
        config->env_capacity = 128;
        config->steps = 3;
        config->stat_res = 1;
        config->popul_res = 1;
        config->output_path = output_path;
        config->spatial_domain_size = 20.0f;
        config->sample_radius = 3.0f;
        config->max_local_density = 8.0f;
        config->spring_constant = 0.2f;
        config->mech_dt = 0.05f;
        config->mech_substeps = 2;
        config->epsilon = 0.1f;
        config->verbosity = 0;
        return config;
    };

    auto config1 = make_config("/tmp/test_sim_3d_smoke_1");
    auto config2 = make_config("/tmp/test_sim_3d_smoke_2");

    std::filesystem::remove_all(config1->output_path);
    std::filesystem::remove_all(config2->output_path);
    std::filesystem::create_directories(config1->output_path);
    std::filesystem::create_directories(config2->output_path);

    SimulationEngine3D engine1(config1);
    auto runData1 = engine1.run(3);

    SimulationEngine3D engine2(config2);
    auto runData2 = engine2.run(3);

    REQUIRE(runData1.generational_stat_report.size() == runData2.generational_stat_report.size());
    REQUIRE(runData1.generational_stat_report.size() == 3);

    for (size_t i = 0; i < runData1.generational_stat_report.size(); ++i) {
        const auto& lhs = runData1.generational_stat_report[i];
        const auto& rhs = runData2.generational_stat_report[i];
        require_approx(lhs.tau, rhs.tau);
        require_approx(lhs.mean_fitness, rhs.mean_fitness);
        require_approx(lhs.mean_mutations, rhs.mean_mutations);
        REQUIRE(lhs.total_living_cells == rhs.total_living_cells);
    }

    const auto snapshot1 = read_binary_file(
        std::filesystem::path(config1->output_path) / "population_data" / "population_generation_1.bin");
    const auto snapshot2 = read_binary_file(
        std::filesystem::path(config2->output_path) / "population_data" / "population_generation_1.bin");
    REQUIRE(snapshot1 == snapshot2);
}

TEST_CASE("SimulationEngine3D is repeatable with the same seed", "[SimulationEngine3D][Determinism]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    auto make_config = [](const std::string& output_path) {
        auto config = std::make_shared<SimulationConfig>();
        config->sim_type = SimulationType::SPATIAL_3D_ABM;
        config->tau_step = 0.05;
        config->seed = 123;
        config->initial_population = 64;
        config->env_capacity = 512;
        config->steps = 100;
        config->stat_res = 1;
        config->popul_res = 5;
        config->output_path = output_path;
        config->spatial_domain_size = 32.0f;
        config->sample_radius = 3.0f;
        config->max_local_density = 8.0f;
        config->spring_constant = 0.2f;
        config->mech_dt = 0.05f;
        config->mech_substeps = 2;
        config->epsilon = 0.1f;
        config->verbosity = 0;
        return config;
    };

    auto config1 = make_config("/tmp/test_sim_3d_determinism_1");
    auto config2 = make_config("/tmp/test_sim_3d_determinism_2");

    std::filesystem::remove_all(config1->output_path);
    std::filesystem::remove_all(config2->output_path);
    std::filesystem::create_directories(config1->output_path);
    std::filesystem::create_directories(config2->output_path);

    SimulationEngine3D engine1(config1);
    auto runData1 = engine1.run(100);

    SimulationEngine3D engine2(config2);
    auto runData2 = engine2.run(100);

    REQUIRE(runData1.generational_stat_report.size() == runData2.generational_stat_report.size());
    for (size_t i = 0; i < runData1.generational_stat_report.size(); ++i) {
        const auto& lhs = runData1.generational_stat_report[i];
        const auto& rhs = runData2.generational_stat_report[i];
        require_approx(lhs.tau, rhs.tau);
        require_approx(lhs.mean_fitness, rhs.mean_fitness);
        require_approx(lhs.fitness_variance, rhs.fitness_variance);
        require_approx(lhs.mean_mutations, rhs.mean_mutations);
        require_approx(lhs.mutations_variance, rhs.mutations_variance);
        REQUIRE(lhs.total_living_cells == rhs.total_living_cells);
    }

    const auto snapshot1 = read_binary_file(
        std::filesystem::path(config1->output_path) / "population_data" / "population_generation_5.bin");
    const auto snapshot2 = read_binary_file(
        std::filesystem::path(config2->output_path) / "population_data" / "population_generation_5.bin");
    REQUIRE(snapshot1 == snapshot2);
}

TEST_CASE("SimulationEngine3D stabilizes near local carrying capacity", "[SimulationEngine3D][Correctness]") {
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, 1);

    auto config = std::make_shared<SimulationConfig>();
    config->sim_type = SimulationType::SPATIAL_3D_ABM;
    config->tau_step = 0.05;
    config->seed = 42;
    config->initial_population = 125;
    config->env_capacity = 1000;
    config->steps = 200;
    config->stat_res = 1;
    config->popul_res = 5;
    config->output_path = "/tmp/test_sim_3d_stabilization";
    config->spatial_domain_size = 24.0f;
    config->sample_radius = 3.0f;
    config->max_local_density = 8.0f;
    config->spring_constant = 0.5f;
    config->mech_dt = 0.1f;
    config->mech_substeps = 5;
    config->epsilon = 0.1f;
    config->verbosity = 0;

    std::filesystem::remove_all(config->output_path);
    std::filesystem::create_directories(config->output_path);

    SimulationEngine3D engine(config);
    auto runData = engine.run(200);

    REQUIRE(runData.generational_stat_report.size() == 10);
    REQUIRE(runData.generational_stat_report.back().total_living_cells > config->initial_population);

    const auto& stats = runData.generational_stat_report;
    const size_t tail_min = std::min({stats[7].total_living_cells, stats[8].total_living_cells, stats[9].total_living_cells});
    const size_t tail_max = std::max({stats[7].total_living_cells, stats[8].total_living_cells, stats[9].total_living_cells});
    REQUIRE(static_cast<double>(tail_max - tail_min) < 0.2 * static_cast<double>(stats.back().total_living_cells));

    const auto snapshot_path =
        std::filesystem::path(config->output_path) / "population_data" / "population_generation_10.bin";
    const auto snapshot = read_population_snapshot(snapshot_path);
    REQUIRE_FALSE(snapshot.empty());
    REQUIRE(snapshot_within_domain(snapshot, config->spatial_domain_size));

    const double mean_local_neighbors = compute_mean_local_neighbors(snapshot, config->sample_radius);
    REQUIRE(mean_local_neighbors == Catch::Approx(config->max_local_density).margin(1.5));
}