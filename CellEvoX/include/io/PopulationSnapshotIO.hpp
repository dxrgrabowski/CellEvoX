#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace CellEvoX::io {

constexpr std::array<char, 8> kPopulationSnapshotMagic = {'C', 'E', 'L', 'X', 'P', 'O', 'P', '1'};
constexpr uint32_t kPopulationSnapshotVersion = 2;
constexpr uint8_t kPopulationSnapshotFlagHasDriverMutationPayload = 0x1;

#pragma pack(push, 1)
struct PopulationSnapshotFileHeaderV1 {
  char magic[8];
  uint32_t version;
  uint32_t record_size;
  double tau;
  uint32_t record_count;
  uint8_t spatial_dimensions;
  uint8_t reserved[11];
};

struct PopulationSnapshotRecordV1 {
  uint32_t id;
  uint32_t parent_id;
  float fitness;
  float x;
  float y;
  float z;
  uint16_t mutations_count;
  uint8_t position_valid;
  uint8_t reserved;
};

struct PopulationSnapshotFileHeader {
  char magic[8];
  uint32_t version;
  uint32_t record_size;
  double tau;
  uint32_t record_count;
  uint32_t driver_mutation_count;
  uint8_t spatial_dimensions;
  uint8_t mutation_record_size;
  uint8_t flags;
  uint8_t reserved[13];
};

struct PopulationSnapshotRecord {
  uint32_t id;
  uint32_t parent_id;
  float fitness;
  float x;
  float y;
  float z;
  uint16_t mutations_count;
  uint16_t driver_mutation_count;
  uint32_t driver_mutation_offset;
  uint8_t position_valid;
  uint8_t reserved[3];
};

struct PopulationSnapshotDriverMutation {
  uint32_t mutation_id;
  uint8_t mutation_type;
};

struct LegacyPopulationSnapshotRecord3D {
  uint32_t id;
  uint32_t parent_id;
  float fitness;
  float x;
  float y;
  float z;
  uint8_t mutations_count;
};
#pragma pack(pop)

static_assert(sizeof(PopulationSnapshotFileHeaderV1) == 40,
              "PopulationSnapshotFileHeaderV1 must stay tightly packed");
static_assert(sizeof(PopulationSnapshotRecordV1) == 28,
              "PopulationSnapshotRecordV1 must stay tightly packed");
static_assert(sizeof(PopulationSnapshotFileHeader) == 48,
              "PopulationSnapshotFileHeader must stay tightly packed");
static_assert(sizeof(PopulationSnapshotRecord) == 36,
              "PopulationSnapshotRecord must stay tightly packed");
static_assert(sizeof(PopulationSnapshotDriverMutation) == 5,
              "PopulationSnapshotDriverMutation must stay tightly packed");
static_assert(sizeof(LegacyPopulationSnapshotRecord3D) == 25,
              "LegacyPopulationSnapshotRecord3D must stay tightly packed");

inline std::string populationSnapshotPath(const std::string& output_path, int generation) {
  return (std::filesystem::path(output_path) / "population_data" /
          ("population_generation_" + std::to_string(generation) + ".bin"))
      .string();
}

inline PopulationSnapshotFileHeader makePopulationSnapshotHeader(
    double tau,
    uint32_t record_count,
    uint8_t spatial_dimensions,
    uint32_t driver_mutation_count = 0) {
  PopulationSnapshotFileHeader header{};
  std::copy(kPopulationSnapshotMagic.begin(), kPopulationSnapshotMagic.end(), header.magic);
  header.version = kPopulationSnapshotVersion;
  header.record_size = sizeof(PopulationSnapshotRecord);
  header.tau = tau;
  header.record_count = record_count;
  header.driver_mutation_count = driver_mutation_count;
  header.spatial_dimensions = spatial_dimensions;
  header.mutation_record_size = sizeof(PopulationSnapshotDriverMutation);
  header.flags = driver_mutation_count > 0 ? kPopulationSnapshotFlagHasDriverMutationPayload : 0;
  return header;
}

inline bool isPopulationSnapshotHeader(const char* magic) {
  return std::equal(kPopulationSnapshotMagic.begin(), kPopulationSnapshotMagic.end(), magic);
}

inline bool hasDriverMutationPayload(const PopulationSnapshotFileHeader& header) {
  return (header.flags & kPopulationSnapshotFlagHasDriverMutationPayload) != 0 &&
         header.driver_mutation_count > 0;
}

inline bool writePopulationSnapshot(
    const std::filesystem::path& path,
    double tau,
    uint8_t spatial_dimensions,
    const std::vector<PopulationSnapshotRecord>& records,
    const std::vector<PopulationSnapshotDriverMutation>& driver_mutations = {}) {
  std::filesystem::create_directories(path.parent_path());

  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  const auto header = makePopulationSnapshotHeader(
      tau,
      static_cast<uint32_t>(records.size()),
      spatial_dimensions,
      static_cast<uint32_t>(driver_mutations.size()));

  file.write(reinterpret_cast<const char*>(&header), sizeof(header));
  if (!records.empty()) {
    file.write(reinterpret_cast<const char*>(records.data()),
               static_cast<std::streamsize>(records.size() * sizeof(PopulationSnapshotRecord)));
  }
  if (!driver_mutations.empty()) {
    file.write(reinterpret_cast<const char*>(driver_mutations.data()),
               static_cast<std::streamsize>(driver_mutations.size() *
                                            sizeof(PopulationSnapshotDriverMutation)));
  }

  return file.good();
}

inline bool readPopulationSnapshot(const std::filesystem::path& path,
                                   PopulationSnapshotFileHeader& header,
                                   std::vector<PopulationSnapshotRecord>& records,
                                   std::vector<PopulationSnapshotDriverMutation>& driver_mutations) {
  records.clear();
  driver_mutations.clear();

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size < 0) {
    return false;
  }
  file.seekg(0, std::ios::beg);

  if (size >= static_cast<std::streamoff>(sizeof(PopulationSnapshotFileHeader))) {
    PopulationSnapshotFileHeader candidate{};
    file.read(reinterpret_cast<char*>(&candidate), sizeof(candidate));
    if (!file.good()) {
      return false;
    }

    if (isPopulationSnapshotHeader(candidate.magic) &&
        candidate.version == kPopulationSnapshotVersion) {
      const auto expected_size =
          static_cast<std::streamoff>(sizeof(PopulationSnapshotFileHeader)) +
          static_cast<std::streamoff>(candidate.record_count) *
              static_cast<std::streamoff>(candidate.record_size) +
          static_cast<std::streamoff>(candidate.driver_mutation_count) *
              static_cast<std::streamoff>(candidate.mutation_record_size);
      if (candidate.record_size != sizeof(PopulationSnapshotRecord) ||
          candidate.mutation_record_size != sizeof(PopulationSnapshotDriverMutation) ||
          expected_size != size) {
        return false;
      }

      header = candidate;
      records.resize(candidate.record_count);
      if (!records.empty()) {
        file.read(reinterpret_cast<char*>(records.data()),
                  static_cast<std::streamsize>(records.size() * sizeof(PopulationSnapshotRecord)));
        if (!file.good()) {
          return false;
        }
      }

      driver_mutations.resize(candidate.driver_mutation_count);
      if (!driver_mutations.empty()) {
        file.read(reinterpret_cast<char*>(driver_mutations.data()),
                  static_cast<std::streamsize>(driver_mutations.size() *
                                               sizeof(PopulationSnapshotDriverMutation)));
        if (!file.good()) {
          return false;
        }
      }

      return true;
    }
  }

  if (size >= static_cast<std::streamoff>(sizeof(PopulationSnapshotFileHeaderV1))) {
    file.clear();
    file.seekg(0, std::ios::beg);

    PopulationSnapshotFileHeaderV1 candidate{};
    file.read(reinterpret_cast<char*>(&candidate), sizeof(candidate));
    if (!file.good()) {
      return false;
    }

    if (isPopulationSnapshotHeader(candidate.magic) && candidate.version == 1) {
      const auto expected_size =
          static_cast<std::streamoff>(sizeof(PopulationSnapshotFileHeaderV1)) +
          static_cast<std::streamoff>(candidate.record_count) *
              static_cast<std::streamoff>(candidate.record_size);
      if (candidate.record_size != sizeof(PopulationSnapshotRecordV1) || expected_size != size) {
        return false;
      }

      header = makePopulationSnapshotHeader(
          candidate.tau, candidate.record_count, candidate.spatial_dimensions, 0);
      records.resize(candidate.record_count);
      if (candidate.record_count > 0) {
        std::vector<PopulationSnapshotRecordV1> legacy_records(candidate.record_count);
        file.read(reinterpret_cast<char*>(legacy_records.data()),
                  static_cast<std::streamsize>(legacy_records.size() *
                                               sizeof(PopulationSnapshotRecordV1)));
        if (!file.good()) {
          return false;
        }

        for (size_t i = 0; i < legacy_records.size(); ++i) {
          records[i] = {legacy_records[i].id,
                        legacy_records[i].parent_id,
                        legacy_records[i].fitness,
                        legacy_records[i].x,
                        legacy_records[i].y,
                        legacy_records[i].z,
                        legacy_records[i].mutations_count,
                        0,
                        0,
                        legacy_records[i].position_valid,
                        {0, 0, 0}};
        }
      }
      return true;
    }
  }

  if (size % static_cast<std::streamoff>(sizeof(LegacyPopulationSnapshotRecord3D)) != 0) {
    return false;
  }

  file.clear();
  file.seekg(0, std::ios::beg);

  std::vector<LegacyPopulationSnapshotRecord3D> legacy_records(
      static_cast<size_t>(size / static_cast<std::streamoff>(sizeof(LegacyPopulationSnapshotRecord3D))));
  if (!legacy_records.empty()) {
    file.read(reinterpret_cast<char*>(legacy_records.data()), static_cast<std::streamsize>(size));
    if (!file.good()) {
      return false;
    }
  }

  header = makePopulationSnapshotHeader(std::numeric_limits<double>::quiet_NaN(),
                                        static_cast<uint32_t>(legacy_records.size()),
                                        3,
                                        0);

  records.reserve(legacy_records.size());
  for (const auto& legacy : legacy_records) {
    records.push_back({legacy.id,
                       legacy.parent_id,
                       legacy.fitness,
                       legacy.x,
                       legacy.y,
                       legacy.z,
                       legacy.mutations_count,
                       0,
                       0,
                       1,
                       {0, 0, 0}});
  }

  return true;
}

inline bool readPopulationSnapshot(const std::filesystem::path& path,
                                   PopulationSnapshotFileHeader& header,
                                   std::vector<PopulationSnapshotRecord>& records) {
  std::vector<PopulationSnapshotDriverMutation> driver_mutations;
  return readPopulationSnapshot(path, header, records, driver_mutations);
}

}  // namespace CellEvoX::io
