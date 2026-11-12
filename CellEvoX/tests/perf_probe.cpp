// Deterministic instruction-count probe (ADR-0011).
//
// Runs one fixed-iteration simulation workload and prints a single machine
// readable line:
//
//   RESULT workload=<name> iterations=<N> checksum=<hex>
//
// That line must carry exactly one `checksum=` substring: the gate rejects a
// line with two as ambiguous instead of guessing which token counts, so a new
// field must not spell `checksum=` anywhere else on it.
//
// The workload setups mirror the "[perf-core]" Catch2 benchmarks in
// tests/bench_simulation.cpp. Iteration counts are explicit: nothing here is
// calibrated from wall-clock time, so retired-instruction totals stay
// comparable between two binaries built from the same source.
//
// Work is driven through SimulationEngine::step() and a final
// SimulationEngine::run(0) instead of run(steps). runSteps() prints a progress
// bar whose text depends on wall-clock time, which would make the instruction
// count irreproducible; run(0) performs the same materialization and Run
// post-processing without the timed loop.

#include <tbb/global_control.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "io/PopulationSnapshotIO.hpp"
#include "systems/CommonPopulationStep.hpp"
#include "systems/SimulationEngine.hpp"

namespace {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

class Checksum {
 public:
  void bytes(const void* data, size_t size) {
    const auto* first = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
      value_ ^= static_cast<uint64_t>(first[i]);
      value_ *= kFnvPrime;
    }
  }

  void u64(uint64_t value) { bytes(&value, sizeof(value)); }
  void u32(uint32_t value) { u64(value); }
  void f32(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
  }
  void f64(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
  }
  void text(std::string_view value) { bytes(value.data(), value.size()); }

  [[nodiscard]] uint64_t value() const { return value_; }

 private:
  uint64_t value_ = kFnvOffsetBasis;
};

std::filesystem::path probeOutputPath(const std::string& stats_dir, std::string_view name) {
  return std::filesystem::path(stats_dir) / std::string(name);
}

// Mirrors makeConfig() in tests/bench_simulation.cpp. The `tau_step = 0.005`
// value is the benchmark's hardcoded constant, i.e. the AGENTS.md tech debt
// "Hardcoded `0.005` tau in Application's analysis path instead of
// config.tau_step"; when that debt is fixed the probe baseline moves with it,
// which is expected rather than a regression. Both sites must move together.
std::shared_ptr<SimulationConfig> makeConfig(const std::string& stats_dir,
                                             size_t population,
                                             size_t env_capacity,
                                             uint32_t seed,
                                             const std::vector<MutationType>& mutations = {}) {
  auto config = std::make_shared<SimulationConfig>();
  config->sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
  config->tau_step = 0.005;
  config->seed = seed;
  config->initial_population = population;
  config->env_capacity = env_capacity;
  config->stat_res = 1000000;
  config->popul_res = 1000000;
  config->graveyard_pruning_interval = 0;
  config->output_path = probeOutputPath(stats_dir, "test_bench_sim").string();
  config->mutations = mutations;
  config->verbosity = 0;
  return config;
}

// Mirrors makeRealistic2DConfig() in tests/bench_simulation.cpp.
std::shared_ptr<SimulationConfig> makeRealistic2DConfig(const std::string& stats_dir,
                                                        size_t population,
                                                        uint32_t seed) {
  auto config = makeConfig(stats_dir, population, population, seed);
  config->stat_res = 1;
  config->popul_res = 2;
  config->graveyard_pruning_interval = 2;
  config->output_path = probeOutputPath(stats_dir, "test_bench_sim_realistic").string();
  config->mutations = {
      {0.01f, 0.0001f, 1, true},
      {-0.01f, 0.01f, 3, false},
  };
  return config;
}

void mixCell(Checksum& checksum, const Cell& cell) {
  checksum.u32(cell.id);
  checksum.u32(cell.parent_id);
  checksum.f32(cell.fitness);
  checksum.f64(cell.death_time);
  checksum.u64(cell.mutations.size());
  for (const auto& mutation : cell.mutations) {
    checksum.u32(mutation.first);
    checksum.u64(mutation.second);
  }
}

template <typename CellTable>
void mixCellTable(Checksum& checksum, const CellTable& table) {
  std::vector<uint32_t> ids;
  ids.reserve(table.size());
  for (auto it = table.begin(); it != table.end(); ++it) {
    ids.push_back(it->first);
  }
  // Hash map iteration order is an implementation detail; sort for stability.
  std::sort(ids.begin(), ids.end());
  checksum.u64(ids.size());
  for (uint32_t id : ids) {
    typename CellTable::const_accessor accessor;
    if (table.find(accessor, id)) {
      mixCell(checksum, accessor->second);
    }
  }
}

template <typename TreeTable>
void mixPhylogeneticTree(Checksum& checksum, const TreeTable& tree) {
  std::vector<uint32_t> ids;
  ids.reserve(tree.size());
  for (auto it = tree.begin(); it != tree.end(); ++it) {
    ids.push_back(it->first);
  }
  // Hash map iteration order is an implementation detail; sort for stability.
  std::sort(ids.begin(), ids.end());
  checksum.u64(ids.size());
  for (uint32_t id : ids) {
    typename TreeTable::const_accessor accessor;
    if (tree.find(accessor, id)) {
      checksum.u32(id);
      checksum.u32(accessor->second.parent_id);
      checksum.u32(accessor->second.child_sum);
      checksum.f64(accessor->second.death_time);
    }
  }
}

// Snapshot files are part of the workload's output. The engine logs and
// continues when a write fails, so without this the bytes on disk are the one
// part of the result the checksum cannot see: a skipped or corrupted write
// would lower the instruction count and could be reported as an improvement.
// The engine writes one snapshot per `generational_popul_report` entry, so a
// missing file means the run itself is broken rather than that the probe
// assumed more than the report promises. Files the report does not reference
// are out of scope: the report is the source of truth for which snapshots this
// workload produced. The format is packed and value-initialized (see
// PopulationSnapshotIO.hpp), so hashing raw bytes compares content rather than
// padding. The generation index is not hashed
// separately: the report is built in a fixed order, which is part of the
// contract this checksum relies on, and a reordering still moves the bytes
// inside the hash. A snapshot ABI version bump changes these bytes on purpose,
// so it reports SEMANTICS CHANGED until the probe and ADR-0011's Evidence are
// updated together.
void mixSnapshotFiles(Checksum& checksum,
                      const std::string& output_path,
                      const std::vector<std::pair<int, CellMap>>& report) {
  checksum.u64(report.size());
  for (const auto& entry : report) {
    const std::string path = CellEvoX::io::populationSnapshotPath(output_path, entry.first);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
      throw std::runtime_error("population snapshot missing: " + path);
    }
    const std::streamoff size = file.tellg();
    if (size < 0) {
      throw std::runtime_error("cannot size population snapshot: " + path);
    }
    file.seekg(0);
    // Stream in fixed-size chunks: this runs inside the collected window, so a
    // large or corrupt snapshot must not size an allocation from its length.
    // Checksum::bytes accumulates byte-by-byte with no per-call framing, so
    // chunking folds the same bytes as one read.
    checksum.u64(static_cast<uint64_t>(size));
    std::vector<char> buffer(64 * 1024);
    std::streamoff remaining = size;
    while (remaining > 0) {
      const auto chunk = static_cast<std::streamsize>(
          std::min<std::streamoff>(remaining, static_cast<std::streamoff>(buffer.size())));
      file.read(buffer.data(), chunk);
      if (file.gcount() != chunk) {
        throw std::runtime_error("population snapshot unreadable: " + path);
      }
      checksum.bytes(buffer.data(), static_cast<size_t>(chunk));
      remaining -= chunk;
    }
  }
}

void mixRun(Checksum& checksum, const ecs::Run& run) {
  checksum.f64(run.tau);
  checksum.u64(run.total_deaths);
  checksum.u64(static_cast<uint64_t>(run.termination_reason));
  mixCellTable(checksum, run.cells);

  std::vector<uint32_t> dead_ids;
  dead_ids.reserve(run.cells_graveyard.size());
  for (auto it = run.cells_graveyard.begin(); it != run.cells_graveyard.end(); ++it) {
    dead_ids.push_back(it->first);
  }
  std::sort(dead_ids.begin(), dead_ids.end());
  checksum.u64(dead_ids.size());
  for (uint32_t id : dead_ids) {
    Graveyard::const_accessor accessor;
    if (run.cells_graveyard.find(accessor, id)) {
      checksum.u32(id);
      checksum.u32(accessor->second.first);
      checksum.f64(accessor->second.second);
    }
  }

  checksum.u64(run.generational_stat_report.size());
  for (const auto& snapshot : run.generational_stat_report) {
    checksum.f64(snapshot.tau);
    checksum.f64(snapshot.mean_fitness);
    checksum.f64(snapshot.fitness_variance);
    checksum.f64(snapshot.mean_mutations);
    checksum.f64(snapshot.mutations_variance);
    checksum.u64(snapshot.total_living_cells);
  }

  checksum.u64(run.generational_popul_report.size());
  for (const auto& [generation, population] : run.generational_popul_report) {
    checksum.u64(static_cast<uint64_t>(generation));
    mixCellTable(checksum, population);
  }

  mixPhylogeneticTree(checksum, run.phylogenetic_tree);
  checksum.u64(run.total_mutations);
  checksum.u64(static_cast<uint64_t>(run.driver_mutations));
  checksum.u64(static_cast<uint64_t>(run.positive_mutations));
  checksum.u64(static_cast<uint64_t>(run.negative_mutations));
  checksum.u64(static_cast<uint64_t>(run.neutral_mutations));
  checksum.f32(run.average_mutations);
}

// Step `steps` times, then materialize through run(0) so the measured work
// includes cell/graveyard materialization and Run post-processing.
uint64_t runStepsWorkload(const std::shared_ptr<SimulationConfig>& config, uint32_t steps) {
  SimulationEngine engine(config);
  for (uint32_t i = 0; i < steps; ++i) {
    engine.step();
  }
  auto run = engine.run(0);
  Checksum checksum;
  mixRun(checksum, run);
  mixSnapshotFiles(checksum, config->output_path, run.generational_popul_report);
  return checksum.value();
}

// Mirrors runCommonPopulationStepHarness() in tests/bench_simulation.cpp.
uint64_t runCommonStepWorkload(const std::string& stats_dir, size_t population) {
  auto config = makeConfig(stats_dir, population, population * 100, 42);
  CellEvoX::systems::DensePopulation cells(population);
  std::map<uint8_t, MutationType> available_mutation_types;
  size_t actual_population = population;
  size_t total_deaths = 0;
  CellEvoX::systems::applyCommonPopulationStep(cells,
                                               *config,
                                               available_mutation_types,
                                               0.0,
                                               actual_population,
                                               total_deaths,
                                               config->tau_step);

  Checksum checksum;
  checksum.u64(actual_population);
  checksum.u64(total_deaths);
  checksum.u64(cells.cells.size());
  // Vector order is part of the contract: reordering `cells` or `alive_cell_ids`
  // changes the checksum and is reported as SEMANTICS CHANGED, not as noise.
  for (const Cell& cell : cells.cells) {
    mixCell(checksum, cell);
  }
  checksum.u64(cells.alive_cell_ids.size());
  for (uint32_t id : cells.alive_cell_ids) {
    checksum.u32(id);
  }
  return checksum.value();
}

uint64_t runWorkloadIteration(std::string_view workload,
                              const std::string& stats_dir,
                              uint32_t seed) {
  if (workload == "stochastic-step-baseline") {
    return runStepsWorkload(makeConfig(stats_dir, 100000, 10000000, seed), 1);
  }
  if (workload == "stochastic-step-mutations") {
    std::vector<MutationType> mutations;
    for (uint8_t i = 0; i < 8; ++i) {
      // Designated initializers: a reordered field in MutationType is a compile
      // error instead of a silently different workload.
      mutations.push_back({
          .effect = 0.05f * static_cast<float>(i + 1),
          .probability = 0.05f,
          .type_id = i,
          .is_driver = (i % 2 == 0),
      });
    }
    return runStepsWorkload(makeConfig(stats_dir, 100000, 10000000, seed, mutations), 1);
  }
  if (workload == "stochastic-step-pressure") {
    return runStepsWorkload(makeConfig(stats_dir, 100000, 100000, seed), 1);
  }
  if (workload == "run-5-steps") {
    return runStepsWorkload(makeConfig(stats_dir, 50000, 1000000, seed), 5);
  }
  if (workload == "common-step-baseline") {
    return runCommonStepWorkload(stats_dir, 100000);
  }
  if (workload == "realistic-2d-450") {
    return runStepsWorkload(makeRealistic2DConfig(stats_dir, 2000, seed), 450);
  }
  throw std::invalid_argument("unknown workload: " + std::string(workload));
}

void printWorkloads() {
  std::cout << "workloads:\n"
            << "  stochastic-step-baseline\n"
            << "  stochastic-step-mutations\n"
            << "  stochastic-step-pressure\n"
            << "  run-5-steps\n"
            << "  common-step-baseline\n"
            << "  realistic-2d-450\n";
}

[[noreturn]] void usageError(std::string_view message) {
  std::cerr << "error: " << message << "\n"
            << "usage: CellEvoXPerfProbe --workload <name> --iterations <N> "
               "[--seed <S>] [--threads 1] [--stats-dir <dir>]\n";
  printWorkloads();
  std::exit(2);
}

// std::stoull/std::stoul throw on malformed or out-of-range input; route that
// to the usage error instead of an uncaught exception.
uint64_t parseUnsigned(std::string_view option, const std::string& text) {
  if (!text.empty() && text[0] == '-') {
    // std::stoull accepts a minus sign and wraps it into a huge value.
    usageError(std::string(option) + " expects an unsigned integer, got '" + text + "'");
  }
  try {
    size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if (consumed == text.size()) {
      return value;
    }
  } catch (const std::exception&) {
  }
  usageError(std::string(option) + " expects an unsigned integer, got '" + text + "'");
}

uint32_t parseUint32(std::string_view option, const std::string& text) {
  const uint64_t value = parseUnsigned(option, text);
  if (value > std::numeric_limits<uint32_t>::max()) {
    usageError(std::string(option) + " is out of range for a 32-bit value, got '" + text + "'");
  }
  return static_cast<uint32_t>(value);
}

}  // namespace

int main(int argc, char** argv) {
  std::string workload;
  std::string stats_dir = "/tmp/cellevox_perf_probe";
  uint64_t iterations = 0;
  uint32_t seed = 42;
  uint32_t threads = 1;

  for (int i = 1; i < argc; ++i) {
    const std::string_view argument = argv[i];
    const bool has_value = (i + 1) < argc;
    if (argument == "--workload" && has_value) {
      workload = argv[++i];
    } else if (argument == "--iterations" && has_value) {
      iterations = parseUnsigned("--iterations", argv[++i]);
    } else if (argument == "--seed" && has_value) {
      seed = parseUint32("--seed", argv[++i]);
    } else if (argument == "--threads" && has_value) {
      threads = parseUint32("--threads", argv[++i]);
    } else if (argument == "--stats-dir" && has_value) {
      stats_dir = argv[++i];
    } else if (argument == "--workload" || argument == "--iterations" ||
               argument == "--seed" || argument == "--threads" ||
               argument == "--stats-dir") {
      // A known flag with no following argument would otherwise fall through to
      // "unrecognised argument", which hides the actual mistake.
      usageError(std::string(argument) + " requires a value");
    } else {
      usageError("unrecognised argument: " + std::string(argument));
    }
  }

  if (workload.empty()) {
    usageError("--workload is required");
  }
  if (iterations == 0) {
    usageError("--iterations must be >= 1");
  }
  if (threads == 0) {
    usageError("--threads must be >= 1");
  }

  // Pin TBB before any task runs so task-to-thread assignment cannot perturb
  // the instruction count.
  tbb::global_control parallelism(tbb::global_control::max_allowed_parallelism, threads);

  // Own a subdirectory instead of clearing the caller's directory: the gate
  // passes a fresh path, but --stats-dir may point at a folder holding other
  // files. Removing only the probe's own output keeps repeat runs reproducible.
  // `<--stats-dir>/perf_probe` is therefore probe-owned: nothing else belongs
  // there, because every run deletes it.
  const std::filesystem::path run_stats_dir = std::filesystem::path(stats_dir) / "perf_probe";
  // Two separate codes: sharing one would let a failed remove_all be masked by
  // a successful create_directories, leaving stale files inside the measured
  // window.
  std::error_code remove_error;
  std::error_code create_error;
  std::filesystem::remove_all(run_stats_dir, remove_error);
  std::filesystem::create_directories(run_stats_dir, create_error);
  if (remove_error || create_error) {
    std::cerr << "error: cannot prepare stats dir '" << run_stats_dir.string() << "': "
              << (remove_error ? remove_error : create_error).message() << "\n";
    return 2;
  }

  Checksum checksum;
  try {
    // The checksum is the fold of each iteration's own checksum, so the value
    // is only comparable between runs with the same --iterations. The gate
    // always passes 1; the RESULT line echoes iterations to make any other
    // value visible in the artifact.
    for (uint64_t i = 0; i < iterations; ++i) {
      checksum.u64(runWorkloadIteration(workload, run_stats_dir.string(), seed));
    }
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 2;
  }

  std::cout << "RESULT workload=" << workload << " iterations=" << iterations << " checksum=0x"
            << std::hex << checksum.value() << std::dec << "\n";
  return 0;
}
