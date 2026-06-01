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

void fillSnapshotMagic(char (&magic)[8]) {
    std::copy(CellEvoX::io::kPopulationSnapshotMagic.begin(),
              CellEvoX::io::kPopulationSnapshotMagic.end(),
              magic);
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

TEST_CASE("PopulationSnapshotIO reads legacy V1 headered snapshots", "[PopulationSnapshotIO][Legacy]") {
    const auto snapshot_path = testTempPath("legacy_population_snapshot_v1.bin");
    std::filesystem::create_directories(snapshot_path.parent_path());

    CellEvoX::io::PopulationSnapshotFileHeaderV1 legacy_header{};
    fillSnapshotMagic(legacy_header.magic);
    legacy_header.version = 1;
    legacy_header.record_size = sizeof(CellEvoX::io::PopulationSnapshotRecordV1);
    legacy_header.tau = 4.25;
    legacy_header.record_count = 1;
    legacy_header.spatial_dimensions = 2;

    CellEvoX::io::PopulationSnapshotRecordV1 legacy_record{};
    legacy_record.id = 9;
    legacy_record.parent_id = 4;
    legacy_record.fitness = 1.5f;
    legacy_record.x = 1.0f;
    legacy_record.y = 2.0f;
    legacy_record.z = 0.0f;
    legacy_record.mutations_count = 7;
    legacy_record.position_valid = 1;

    {
        std::ofstream file(snapshot_path, std::ios::binary);
        REQUIRE(file.is_open());
        writePod(file, legacy_header);
        writePod(file, legacy_record);
    }

    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> records;
    std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutations;
    REQUIRE(CellEvoX::io::readPopulationSnapshot(snapshot_path, header, records, mutations));

    REQUIRE(header.version == CellEvoX::io::kPopulationSnapshotVersion);
    REQUIRE(header.tau == Catch::Approx(4.25));
    REQUIRE(header.record_count == 1);
    REQUIRE(header.spatial_dimensions == 2);
    REQUIRE_FALSE(CellEvoX::io::hasAnyMutationPayload(header));
    REQUIRE(records.size() == 1);
    REQUIRE(mutations.empty());
    REQUIRE(records[0].id == 9);
    REQUIRE(records[0].parent_id == 4);
    REQUIRE(records[0].fitness == Catch::Approx(1.5f));
    REQUIRE(records[0].x == Catch::Approx(1.0f));
    REQUIRE(records[0].y == Catch::Approx(2.0f));
    REQUIRE(records[0].mutations_count == 7);
    REQUIRE(records[0].driver_mutation_count == 0);
    REQUIRE(records[0].driver_mutation_offset == 0);
    REQUIRE(records[0].position_valid == 1);
}

TEST_CASE("PopulationSnapshotIO reads raw legacy 3D snapshots", "[PopulationSnapshotIO][Legacy]") {
    const auto snapshot_path = testTempPath("legacy_population_snapshot_raw_3d.bin");
    std::filesystem::create_directories(snapshot_path.parent_path());

    const std::vector<CellEvoX::io::LegacyPopulationSnapshotRecord3D> legacy_records = {
        {5, 1, 1.25f, 10.0f, 11.0f, 12.0f, 2},
        {6, 5, 0.75f, 13.0f, 14.0f, 15.0f, 3},
    };

    {
        std::ofstream file(snapshot_path, std::ios::binary);
        REQUIRE(file.is_open());
        file.write(reinterpret_cast<const char*>(legacy_records.data()),
                   static_cast<std::streamsize>(legacy_records.size() * sizeof(legacy_records[0])));
        REQUIRE(file.good());
    }

    CellEvoX::io::PopulationSnapshotFileHeader header{};
    std::vector<CellEvoX::io::PopulationSnapshotRecord> records;
    std::vector<CellEvoX::io::PopulationSnapshotDriverMutation> mutations;
    REQUIRE(CellEvoX::io::readPopulationSnapshot(snapshot_path, header, records, mutations));

    REQUIRE(header.version == CellEvoX::io::kPopulationSnapshotVersion);
    REQUIRE(std::isnan(header.tau));
    REQUIRE(header.record_count == legacy_records.size());
    REQUIRE(header.spatial_dimensions == 3);
    REQUIRE_FALSE(CellEvoX::io::hasAnyMutationPayload(header));
    REQUIRE(mutations.empty());
    REQUIRE(records.size() == 2);
    REQUIRE(records[0].id == 5);
    REQUIRE(records[0].parent_id == 1);
    REQUIRE(records[0].fitness == Catch::Approx(1.25f));
    REQUIRE(records[0].x == Catch::Approx(10.0f));
    REQUIRE(records[0].position_valid == 1);
    REQUIRE(records[1].id == 6);
    REQUIRE(records[1].mutations_count == 3);
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
