#pragma once
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
#include <unordered_map>
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
    case SimulationType::SPATIAL_3D_ABM:
      return "SPATIAL_3D_ABM";
    default:
      return "UNKNOWN";
  }
}

inline SimulationConfig fromJson(const nlohmann::json& j) {
  SimulationConfig config;
  spdlog::info("Parsing simulation configuration from JSON");
  try {
    config.sim_type = j.at("stochastic") ? SimulationType::STOCHASTIC_TAU_LEAP
                                         : SimulationType::DETERMINISTIC_RK4;
    if (j.contains("simulation_mode") && j["simulation_mode"] == "spatial_3d") {
      config.sim_type = SimulationType::SPATIAL_3D_ABM;
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
    config.output_path = j.at("output_path");
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
      config.mutations.push_back({mut.at("effect"),
                                  mut.at("probability"),
                                  mut.at("id"),
                                  mut.at("is_driver")});
    }
  } catch (const nlohmann::json::exception& e) {
    spdlog::error("Error parsing JSON: {}", e.what());
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
  spdlog::info("Statistics resolution: {}", config.stat_res);
  spdlog::info("Population statistics resolution: {}", config.popul_res);
  spdlog::info("Graveyard pruning interval: {}", config.graveyard_pruning_interval);
  spdlog::info("Output path: {}", config.output_path);
  spdlog::info("Phylogeny num cells: {}", config.phylogeny_num_cells_sampling);
  if (config.sim_type == SimulationType::SPATIAL_3D_ABM) {
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