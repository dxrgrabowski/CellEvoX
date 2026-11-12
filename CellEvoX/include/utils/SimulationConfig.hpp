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

  if (config.sim_type == SimulationType::SPATIAL_3D_CAPACITY) {
    requirePositive(config.spatial_domain_size, "spatial_domain_size");
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
    requireFinite(mutation.probability, "mutations[].probability");
    if (mutation.probability < 0.0f || mutation.probability > 1.0f) {
      throw std::runtime_error(
          "Invalid simulation config: mutation probability must be in [0, 1]");
    }
    if (!mutation_ids.insert(mutation.type_id).second) {
      throw std::runtime_error("Invalid simulation config: duplicate mutation id");
    }

    switch (mutation.effect_mode) {
      case MutationEffectMode::Constant:
        requireFinite(mutation.effect, "mutations[].effect");
        if (mutation.effect <= -1.0f) {
          throw std::runtime_error(
              "Invalid simulation config: mutation effect must keep daughter fitness positive");
        }
        break;
      case MutationEffectMode::Gamma:
        requireFinite(mutation.effect_mean, "mutations[].effect_mean");
        requireFinite(mutation.effect_std, "mutations[].effect_std");
        requireFinite(mutation.effect_limit, "mutations[].effect_limit");
        if (!(mutation.effect_mean > 0.0f)) {
          throw std::runtime_error(
              "Invalid simulation config: mutation effect_mean must be positive");
        }
        if (!(mutation.effect_std > 0.0f)) {
          throw std::runtime_error(
              "Invalid simulation config: mutation effect_std must be positive");
        }
        if (!(mutation.effect_limit > 0.0f) || !(mutation.effect_limit < 1.0f)) {
          throw std::runtime_error(
              "Invalid simulation config: mutation effect_limit must be in (0, 1) so that "
              "daughter fitness stays positive");
        }
        if (mutation.effect_limit < mutation.effect_mean) {
          throw std::runtime_error(
              "Invalid simulation config: mutation effect_limit must be at least effect_mean");
        }
        if (mutation.effect_direction != MutationEffectDirection::Positive &&
            mutation.effect_direction != MutationEffectDirection::Negative) {
          throw std::runtime_error(
              "Invalid simulation config: mutation effect_direction must be 'positive' or "
              "'negative'");
        }
        break;
      default:
        throw std::runtime_error("Invalid simulation config: unknown mutation effect_mode");
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
    // The removed deterministic mode must be rejected even when a valid
    // simulation_mode is present alongside the legacy flag.
    if (j.contains("stochastic") && !j.at("stochastic").get<bool>()) {
      throw std::runtime_error(
          "Unsupported legacy 'stochastic: false'. "
          "Supported modes: stochastic, spatial_3d_capacity");
    }
    if (j.contains("simulation_mode")) {
      const std::string simulation_mode = j["simulation_mode"];
      if (simulation_mode == "stochastic") {
        config.sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
      } else if (simulation_mode == "spatial_3d_capacity") {
        config.sim_type = SimulationType::SPATIAL_3D_CAPACITY;
      } else {
        throw std::runtime_error(
            "Unsupported simulation_mode '" + simulation_mode +
            "'. Supported modes: stochastic, spatial_3d_capacity");
      }
    } else if (j.contains("stochastic")) {
      config.sim_type = SimulationType::STOCHASTIC_TAU_LEAP;
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
      MutationType mutation_type{};
      mutation_type.probability = mut.at("probability").get<float>();
      mutation_type.type_id = static_cast<uint8_t>(mutation_id);
      mutation_type.is_driver = mut.at("is_driver").get<bool>();

      // A missing effect_mode means "constant" so existing configs keep working.
      const std::string effect_mode =
          mut.contains("effect_mode") ? mut.at("effect_mode").get<std::string>() : "constant";
      if (effect_mode == "constant") {
        mutation_type.effect_mode = MutationEffectMode::Constant;
        mutation_type.effect = mut.at("effect").get<float>();
      } else if (effect_mode == "gamma") {
        mutation_type.effect_mode = MutationEffectMode::Gamma;
        mutation_type.effect_mean = mut.at("effect_mean").get<float>();
        mutation_type.effect_std = mut.at("effect_std").get<float>();
        mutation_type.effect_limit = mut.at("effect_limit").get<float>();
        const std::string effect_direction = mut.at("effect_direction").get<std::string>();
        if (effect_direction == "positive") {
          mutation_type.effect_direction = MutationEffectDirection::Positive;
        } else if (effect_direction == "negative") {
          mutation_type.effect_direction = MutationEffectDirection::Negative;
        } else {
          throw std::runtime_error(
              "Invalid simulation config: mutation effect_direction must be 'positive' or "
              "'negative'");
        }
      } else {
        throw std::runtime_error(
            "Invalid simulation config: mutation effect_mode must be 'constant' or 'gamma'");
      }
      config.mutations.push_back(mutation_type);
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
  if (config.sim_type == SimulationType::SPATIAL_3D_CAPACITY) {
    spdlog::info("Spatial domain size: {:.2f}", config.spatial_domain_size);
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
