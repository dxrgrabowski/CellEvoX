#pragma once
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ecs/Cell.hpp"
#include "systems/SimulationEngine.hpp"

namespace utils {

inline const char* toString(SimulationType type) {
  switch (type) {
    case SimulationType::STOCHASTIC_TAU_LEAP:
      return "STOCHASTIC_TAU_LEAP";
    case SimulationType::DETERMINISTIC_RK4:
      return "DETERMINISTIC_RK4";
    case SimulationType::SPATIAL_3D_DENSITY:
      return "SPATIAL_3D_DENSITY";
    case SimulationType::SPATIAL_3D_CAPACITY:
      return "SPATIAL_3D_CAPACITY";
    default:
      return "UNKNOWN";
  }
}

inline void requireFinite(double value, const char* field_name) {
  if (!std::isfinite(value)) {
    throw std::runtime_error(std::string("Invalid simulation config: ") + field_name +
                             " must be finite");
  }
}

inline void requirePositive(double value, const char* field_name) {
  requireFinite(value, field_name);
  if (value <= 0.0) {
    throw std::runtime_error(std::string("Invalid simulation config: ") + field_name +
                             " must be positive");
  }
}

inline void requireNonNegative(double value, const char* field_name) {
  requireFinite(value, field_name);
  if (value < 0.0) {
    throw std::runtime_error(std::string("Invalid simulation config: ") + field_name +
                             " must be non-negative");
  }
}

inline void validateConfig(const SimulationConfig& config) {
  requirePositive(config.tau_step, "tau_step");
  if (config.initial_population > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error(
        "Invalid simulation config: initial_population exceeds uint32_t cell id space");
  }
  if (config.env_capacity == 0) {
    throw std::runtime_error("Invalid simulation config: env_capacity must be positive");
  }
  if (config.stat_res == 0) {
    throw std::runtime_error(
        "Invalid simulation config: statistics_resolution must be positive");
  }
  if (config.popul_res == 0) {
    throw std::runtime_error(
        "Invalid simulation config: population_statistics_res must be positive");
  }
  if (config.graveyard_pruning_interval < 0) {
    throw std::runtime_error(
        "Invalid simulation config: graveyard_pruning_interval must be non-negative");
  }
  if (config.output_path.empty()) {
    throw std::runtime_error("Invalid simulation config: output_path must not be empty");
  }

  if (config.sim_type == SimulationType::SPATIAL_3D_DENSITY ||
      config.sim_type == SimulationType::SPATIAL_3D_CAPACITY) {
    requirePositive(config.spatial_domain_size, "spatial_domain_size");
    requirePositive(config.max_local_density, "max_local_density");
    requirePositive(config.sample_radius, "sample_radius");
    requireNonNegative(config.spring_constant, "spring_constant");
    requirePositive(config.mech_dt, "mech_dt");
    if (config.mech_substeps <= 0) {
      throw std::runtime_error("Invalid simulation config: mech_substeps must be positive");
    }
    requireNonNegative(config.epsilon, "epsilon");
  }

  double total_mutation_probability = 0.0;
  std::unordered_set<uint8_t> mutation_ids;
  for (const auto& mutation : config.mutations) {
    requireFinite(mutation.effect, "mutations[].effect");
    requireFinite(mutation.probability, "mutations[].probability");
    if (mutation.effect <= -1.0f) {
      throw std::runtime_error(
          "Invalid simulation config: mutation effect must keep daughter fitness positive");
    }
    if (mutation.probability < 0.0f || mutation.probability > 1.0f) {
      throw std::runtime_error(
          "Invalid simulation config: mutation probability must be in [0, 1]");
    }
    if (!mutation_ids.insert(mutation.type_id).second) {
      throw std::runtime_error("Invalid simulation config: duplicate mutation id");
    }
    total_mutation_probability += mutation.probability;
  }
  if (total_mutation_probability > 1.0 + 1e-9) {
    throw std::runtime_error(
        "Invalid simulation config: total mutation probability must not exceed 1");
  }
}

inline SimulationConfig fromJson(const nlohmann::json& j) {
  SimulationConfig config;
  spdlog::info("Parsing simulation configuration from JSON");
  try {
    if (j.contains("simulation_mode")) {
      const std::string simulation_mode = j["simulation_mode"];
      if (simulation_mode == "stochastic") {
        config.sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
      } else if (simulation_mode == "deterministic") {
        config.sim_type = SimulationType::DETERMINISTIC_RK4;
      } else if (simulation_mode == "spatial_3d_density" || simulation_mode == "spatial_3d") {
        config.sim_type = SimulationType::SPATIAL_3D_DENSITY;
      } else if (simulation_mode == "spatial_3d_capacity") {
        config.sim_type = SimulationType::SPATIAL_3D_CAPACITY;
      } else {
        spdlog::warn("Unknown simulation_mode '{}'; defaulting to stochastic tau-leap", simulation_mode);
      }
    } else if (j.contains("stochastic")) {
      config.sim_type = j.at("stochastic") ? SimulationType::STOCHASTIC_TAU_LEAP
                                           : SimulationType::DETERMINISTIC_RK4;
      spdlog::warn("Config uses legacy 'stochastic' field; prefer 'simulation_mode'");
    }
    config.tau_step = j.at("tau_step");
    if (j.contains("seed")) {
      config.seed = j.at("seed");
    } else {
      config.seed = 42;
    }
    config.initial_population = j.at("initial_population");
    config.env_capacity = j.at("env_capacity");
    config.steps = j.at("steps");
    config.stat_res = j.at("statistics_resolution");
    config.popul_res = j.at("population_statistics_res");
    if (j.contains("graveyard_pruning_interval")) {
      config.graveyard_pruning_interval = j.at("graveyard_pruning_interval");
    } else {
      config.graveyard_pruning_interval = 0;
    }
    if (j.contains("max_population_cutoff")) {
      config.max_population_cutoff = j.at("max_population_cutoff");
    } else {
      config.max_population_cutoff = 0;  // disabled by default
    }
    config.output_path = j.at("output_path");
    if (j.contains("full_mutation_payload")) {
      config.full_mutation_payload = j.at("full_mutation_payload");
    } else if (j.contains("snapshot_full_mutation_payload")) {
      config.full_mutation_payload = j.at("snapshot_full_mutation_payload");
    }
    if (j.contains("verbosity")) {
      config.verbosity = j.at("verbosity");
    } else {
      config.verbosity = 2; // Default to full logging
    }
    if (j.contains("phylogeny_num_cells_sampling")) {
      config.phylogeny_num_cells_sampling = j.at("phylogeny_num_cells_sampling");
    } else {
      config.phylogeny_num_cells_sampling = 100;
    }
    if (j.contains("spatial_domain_size")) {
      config.spatial_domain_size = j.at("spatial_domain_size");
    }
    if (j.contains("max_local_density")) {
      config.max_local_density = j.at("max_local_density");
    }
    if (j.contains("sample_radius")) {
      config.sample_radius = j.at("sample_radius");
    }
    if (j.contains("spring_constant")) {
      config.spring_constant = j.at("spring_constant");
    }
    if (j.contains("mech_dt")) {
      config.mech_dt = j.at("mech_dt");
    }
    if (j.contains("mech_substeps")) {
      config.mech_substeps = j.at("mech_substeps");
    }
    if (j.contains("epsilon")) {
      config.epsilon = j.at("epsilon");
    }

    for (const auto& mut : j.at("mutations")) {
      const auto mutation_id = mut.at("id").get<int>();
      if (mutation_id < 0 ||
          mutation_id > static_cast<int>(std::numeric_limits<uint8_t>::max())) {
        throw std::runtime_error("Invalid simulation config: mutation id must fit in uint8_t");
      }
      config.mutations.push_back({mut.at("effect").get<float>(),
                                  mut.at("probability").get<float>(),
                                  static_cast<uint8_t>(mutation_id),
                                  mut.at("is_driver").get<bool>()});
    }
    validateConfig(config);
  } catch (const nlohmann::json::exception& e) {
    spdlog::error("Error parsing JSON: {}", e.what());
    throw;
  } catch (const std::runtime_error& e) {
    spdlog::error("Error validating JSON config: {}", e.what());
    throw;
  }
  spdlog::info("Successfully parsed simulation configuration");
  return config;
}

inline void printConfig(const SimulationConfig& config) {
  spdlog::info("Simulation configuration:");
  spdlog::info("Seed: {}", config.seed);
  spdlog::info("Simulation type: {}", toString(config.sim_type));
  spdlog::info("Tau step: {:.3f}", config.tau_step);
  spdlog::info("Initial population: {}", config.initial_population);
  spdlog::info("Environment capacity: {}", config.env_capacity);
  spdlog::info("Number of steps: {}", config.steps);
  spdlog::info("Statistics resolution: {}", config.stat_res);
  spdlog::info("Population statistics resolution: {}", config.popul_res);
  spdlog::info("Graveyard pruning interval: {}", config.graveyard_pruning_interval);
  if (config.max_population_cutoff > 0) {
    spdlog::info("Max population cutoff: {} (simulation stops when N >= this value)",
                 config.max_population_cutoff);
  } else {
    spdlog::info("Max population cutoff: disabled");
  }
  spdlog::info("Output path: {}", config.output_path);
  spdlog::info("Full mutation payload snapshots: {}", config.full_mutation_payload);
  spdlog::info("Phylogeny num cells: {}", config.phylogeny_num_cells_sampling);
  if (config.sim_type == SimulationType::SPATIAL_3D_DENSITY ||
      config.sim_type == SimulationType::SPATIAL_3D_CAPACITY) {
    spdlog::info("Spatial domain size: {:.2f}", config.spatial_domain_size);
    spdlog::info("Max local density: {:.2f}", config.max_local_density);
    spdlog::info("Sample radius: {:.2f}", config.sample_radius);
    spdlog::info("Spring constant: {:.2f}", config.spring_constant);
    spdlog::info("Mechanical dt: {:.3f}", config.mech_dt);
    spdlog::info("Mechanical substeps: {}", config.mech_substeps);
    spdlog::info("Division epsilon: {:.3f}", config.epsilon);
  }
  spdlog::info("Mutations:");
  for (const auto& mut : config.mutations) {
    spdlog::info("    {}mutation with id: {}, effect: {:.2f}, probability: {:.3f}",
                 mut.is_driver ? "DRIVER " : "",
                 mut.type_id,
                 mut.effect,
                 mut.probability);
  }
}

}  // namespace utils
