#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "io/PopulationSnapshotIO.hpp"

namespace {

std::filesystem::path testTempPath(std::string_view name) {
    return std::filesystem::temp_directory_path() / "cellevox_tests" / name;
}

template <typename T>
void writePod(std::ofstream& file, const T& value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(T));
    REQUIRE(file.good());
}

}  // namespace

TEST_CASE("PopulationSnapshotIO reports any mutation payload kind", "[PopulationSnapshotIO]") {
    auto empty_header = CellEvoX::io::makePopulationSnapshotHeader(1.0, 0, 0, 0);
    REQUIRE_FALSE(CellEvoX::io::hasAnyMutationPayload(empty_header));

    auto driver_header = CellEvoX::io::makePopulationSnapshotHeader(
        1.0, 1, 0, 1, CellEvoX::io::MutationPayloadKind::DriverOnly);
    REQUIRE(CellEvoX::io::hasAnyMutationPayload(driver_header));
    REQUIRE(CellEvoX::io::hasDriverMutationPayload(driver_header));
    REQUIRE_FALSE(CellEvoX::io::hasFullMutationPayload(driver_header));

    auto full_header = CellEvoX::io::makePopulationSnapshotHeader(
        1.0, 1, 0, 1, CellEvoX::io::MutationPayloadKind::Full);
    REQUIRE(CellEvoX::io::hasAnyMutationPayload(full_header));
    REQUIRE(CellEvoX::io::hasFullMutationPayload(full_header));
    REQUIRE_FALSE(CellEvoX::io::hasDriverMutationPayload(full_header));
}

TEST_CASE("PopulationSnapshotIO clears outputs and fails for missing files", "[PopulationSnapshotIO]") {
    const auto snapshot_path = testTempPath("missing_population_snapshot.bin");
    std::filesystem::remove(snapshot_path);

    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> records = {
        {1, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, {0, 0, 0}}
    };
    std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutations = {{11, 2}};

    REQUIRE_FALSE(CellEvoX::io::readPopulationSnapshot(snapshot_path, header, records, mutations));
    REQUIRE(records.empty());
    REQUIRE(mutations.empty());
}

TEST_CASE("PopulationSnapshotIO rejects truncated V2 snapshots", "[PopulationSnapshotIO]") {
    const auto snapshot_path = testTempPath("truncated_population_snapshot_v2.bin");
    std::filesystem::create_directories(snapshot_path.parent_path());

    const auto header = CellEvoX::io::makePopulationSnapshotHeader(2.5, 1, 3, 0);
    {
        std::ofstream file(snapshot_path, std::ios::binary);
        REQUIRE(file.is_open());
        writePod(file, header);
    }

    CellEvoX::io::PopulationSnapshotFileHeader loaded_header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> records;
    std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutations;
    REQUIRE_FALSE(CellEvoX::io::readPopulationSnapshot(
        snapshot_path, loaded_header, records, mutations));
}

namespace {

std::filesystem::path fixturePath(std::string_view relative) {
    return std::filesystem::path(CELLEVOX_SOURCE_DIR) / "tests" / "fixtures" / "mini_run" /
           relative;
}

std::streamoff fileSize(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    REQUIRE_FALSE(ec);
    return static_cast<std::streamoff>(size);
}

}  // namespace

TEST_CASE("PopulationSnapshotIO serialized sizes match the ABI contract", "[PopulationSnapshotIO]") {
    REQUIRE(sizeof(CellEvoX::io::PopulationSnapshotFileHeader) == 48);
    REQUIRE(sizeof(CellEvoX::io::PopulationSnapshotRecord) == 36);
    REQUIRE(sizeof(CellEvoX::io::PopulationSnapshotDriverMutation) == 5);
    REQUIRE(CellEvoX::io::makePopulationSnapshotHeader(1.0, 0, 0, 0).record_size == 36);
    REQUIRE(CellEvoX::io::makePopulationSnapshotHeader(1.0, 0, 0, 0).mutation_record_size == 5);
}

TEST_CASE("PopulationSnapshotIO reads the driver-only snapshot fixture", "[PopulationSnapshotIO]") {
    const auto snapshot_path = fixturePath("population_data/population_generation_5.bin");

    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> records;
    std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutations;
    REQUIRE(CellEvoX::io::readPopulationSnapshot(snapshot_path, header, records, mutations));

    REQUIRE(std::equal(CellEvoX::io::kPopulationSnapshotMagic.begin(),
                       CellEvoX::io::kPopulationSnapshotMagic.end(),
                       header.magic));
    REQUIRE(header.version == 2);
    REQUIRE(header.record_size == 36);
    REQUIRE(header.mutation_record_size == 5);
    REQUIRE(header.tau == Catch::Approx(5.0));
    REQUIRE(header.record_count == 3);
    REQUIRE(header.driver_mutation_count == 4);
    REQUIRE(header.spatial_dimensions == 0);
    REQUIRE(header.flags == CellEvoX::io::kPopulationSnapshotFlagHasDriverMutationPayload);
    REQUIRE(CellEvoX::io::hasDriverMutationPayload(header));
    REQUIRE_FALSE(CellEvoX::io::hasFullMutationPayload(header));

    REQUIRE(fileSize(snapshot_path) == 48 + 3 * 36 + 4 * 5);

    REQUIRE(records.size() == 3);
    REQUIRE(records[0].id == 0);
    REQUIRE(records[0].parent_id == 0);
    REQUIRE(records[0].fitness == Catch::Approx(1.0f));
    REQUIRE(std::isnan(records[0].x));
    REQUIRE(std::isnan(records[0].y));
    REQUIRE(std::isnan(records[0].z));
    REQUIRE(records[0].mutations_count == 1);
    REQUIRE(records[0].driver_mutation_count == 1);
    REQUIRE(records[0].driver_mutation_offset == 0);
    REQUIRE(records[0].position_valid == 0);

    REQUIRE(records[1].id == 1);
    REQUIRE(records[1].parent_id == 0);
    REQUIRE(records[1].mutations_count == 2);
    REQUIRE(records[1].driver_mutation_count == 2);
    REQUIRE(records[1].driver_mutation_offset == 1);

    REQUIRE(records[2].id == 2);
    REQUIRE(records[2].parent_id == 1);
    REQUIRE(records[2].mutations_count == 1);
    REQUIRE(records[2].driver_mutation_count == 1);
    REQUIRE(records[2].driver_mutation_offset == 3);

    REQUIRE(mutations.size() == 4);
    REQUIRE(mutations[0].mutation_id == 0);
    REQUIRE(mutations[0].mutation_type == 1);
    REQUIRE(mutations[2].mutation_id == 1);
    REQUIRE(mutations[2].mutation_type == 2);

    REQUIRE(records[2].driver_mutation_offset + records[2].driver_mutation_count ==
            mutations.size());
}

TEST_CASE("PopulationSnapshotIO reads the full-payload snapshot fixture", "[PopulationSnapshotIO]") {
    const auto snapshot_path = fixturePath("population_data/population_generation_10.bin");

    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> records;
    std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutations;
    REQUIRE(CellEvoX::io::readPopulationSnapshot(snapshot_path, header, records, mutations));

    REQUIRE(header.version == 2);
    REQUIRE(header.tau == Catch::Approx(10.0));
    REQUIRE(header.record_count == 2);
    REQUIRE(header.driver_mutation_count == 4);
    REQUIRE(header.spatial_dimensions == 3);
    REQUIRE(header.flags == CellEvoX::io::kPopulationSnapshotFlagHasFullMutationPayload);
    REQUIRE(CellEvoX::io::hasFullMutationPayload(header));
    REQUIRE_FALSE(CellEvoX::io::hasDriverMutationPayload(header));

    REQUIRE(fileSize(snapshot_path) == 48 + 2 * 36 + 4 * 5);

    REQUIRE(records.size() == 2);
    REQUIRE(records[0].id == 7);
    REQUIRE(records[0].parent_id == 3);
    REQUIRE(records[0].fitness == Catch::Approx(1.5f));
    REQUIRE(records[0].x == Catch::Approx(1.5f));
    REQUIRE(records[0].y == Catch::Approx(2.25f));
    REQUIRE(records[0].z == Catch::Approx(3.0f));
    REQUIRE(records[0].mutations_count == 2);
    REQUIRE(records[0].driver_mutation_count == 2);
    REQUIRE(records[0].driver_mutation_offset == 0);
    REQUIRE(records[0].position_valid == 1);

    REQUIRE(records[1].id == 8);
    REQUIRE(records[1].parent_id == 7);
    REQUIRE(records[1].mutations_count == 2);
    REQUIRE(records[1].driver_mutation_count == 2);
    REQUIRE(records[1].driver_mutation_offset == 2);

    REQUIRE(mutations.size() == 4);
    REQUIRE(mutations[3].mutation_id == 8);
    REQUIRE(mutations[3].mutation_type == 4);
}

TEST_CASE("PopulationSnapshotIO rejects the invalid snapshot fixtures", "[PopulationSnapshotIO]") {
    for (const std::string_view name : {"truncated_v2.bin", "unknown_version.bin"}) {
        const auto snapshot_path = fixturePath(std::string("invalid/") + std::string(name));

        CellEvoX::io::PopulationSnapshotFileHeader header{};
        std::vector<CellEvoX::io::PopulationSnapshotRecord> records;
        std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutations;
        REQUIRE_FALSE(
            CellEvoX::io::readPopulationSnapshot(snapshot_path, header, records, mutations));
        REQUIRE(records.empty());
        REQUIRE(mutations.empty());
    }
}

TEST_CASE("PopulationSnapshotIO rejects unknown snapshot sizes", "[PopulationSnapshotIO]") {
    const auto snapshot_path = testTempPath("unknown_population_snapshot_size.bin");
    std::filesystem::create_directories(snapshot_path.parent_path());

    {
        std::ofstream file(snapshot_path, std::ios::binary);
        REQUIRE(file.is_open());
        const char bytes[] = {'b', 'a', 'd'};
        file.write(bytes, sizeof(bytes));
        REQUIRE(file.good());
    }

    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> records;
    REQUIRE_FALSE(CellEvoX::io::readPopulationSnapshot(snapshot_path, header, records));
}
