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
    default:
      return "UNKNOWN";
  }
}

inline MutationVariant stringToMutationVariant(const std::string& type) {
  static const std::unordered_map<std::string, MutationVariant> typeMap = {
      {"DRIVER", MutationVariant::DRIVER},
      {"POSITIVE", MutationVariant::POSITIVE},
      {"NEUTRAL", MutationVariant::NEUTRAL},
      {"NEGATIVE", MutationVariant::NEGATIVE}};
  return typeMap.at(type);
}
inline std::string toString(MutationVariant type) {
  switch (type) {
    case MutationVariant::DRIVER:
      return "DRIVER";
    case MutationVariant::POSITIVE:
      return "POSITIVE";
    case MutationVariant::NEUTRAL:
      return "NEUTRAL";
    case MutationVariant::NEGATIVE:
      return "NEGATIVE";
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
    config.tau_step = j.at("tau_step");
    config.initial_population = j.at("initial_population");
    config.env_capacity = j.at("env_capacity");
    config.steps = j.at("steps");
    config.stat_res = j.at("statistics_resolution");
    config.popul_res = j.at("population_statistics_res");
    config.output_path = j.at("output_path");
    for (const auto& mut : j.at("mutations")) {
      config.mutations.push_back({mut.at("effect"),
                                  mut.at("probability"),
                                  mut.at("id"),
                                  stringToMutationVariant(mut.at("type"))});
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
  spdlog::info("Simulation type: {}", toString(config.sim_type));
  spdlog::info("Tau step: {:.3f}", config.tau_step);
  spdlog::info("Initial population: {}", config.initial_population);
  spdlog::info("Environment capacity: {}", config.env_capacity);
  spdlog::info("Number of steps: {}", config.steps);
  spdlog::info("Statistics resolution: {}", config.stat_res);
  spdlog::info("Population statistics resolution: {}", config.popul_res);
  spdlog::info("Output path: {}", config.output_path);
  spdlog::info("Mutations:");
  for (const auto& mut : config.mutations) {
    spdlog::info("    {} mutation with id: {}, effect: {:.2f}, probability: {:.3f}",
                 toString(mut.type),
                 mut.type_id,
                 mut.effect,
                 mut.probability);
  }
}

}  // namespace utils