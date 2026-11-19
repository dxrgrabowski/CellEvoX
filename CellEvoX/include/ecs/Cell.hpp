#pragma once
#include <cstdint>
#include <utility>
#include <vector>

enum class MutationEffectMode : uint8_t {
  Constant = 0,
  Gamma = 1,
};

enum class MutationEffectDirection : uint8_t {
  Positive = 0,
  Negative = 1,
};

struct MutationType {
  float effect{0.0f};
  float probability{0.0f};
  uint8_t type_id{0};  // up to 256 mutationTypes
  bool is_driver{false};

  // Effect model. `effect_mode == Constant` uses `effect` exactly as before.
  // `effect_mode == Gamma` samples a magnitude from Gamma(k, theta) with
  // k = (mean/std)^2, theta = std^2/mean, clamps it to `effect_limit` and
  // applies `direction` as the sign.
  MutationEffectMode effect_mode{MutationEffectMode::Constant};
  float effect_mean{0.0f};
  float effect_std{0.0f};
  MutationEffectDirection effect_direction{MutationEffectDirection::Positive};
  float effect_limit{0.0f};

  double gammaShape() const {
    const double ratio = static_cast<double>(effect_mean) / static_cast<double>(effect_std);
    return ratio * ratio;
  }

  double gammaScale() const {
    return static_cast<double>(effect_std) * static_cast<double>(effect_std) /
           static_cast<double>(effect_mean);
  }

  double directionSign() const {
    return effect_direction == MutationEffectDirection::Negative ? -1.0 : 1.0;
  }
};

// One realized mutation occurrence on a cell.
// `mutation_id` is the id of the parent where the mutation was created,
// `mutation_type` is the MutationType::type_id, `effect` is the realized
// signed fitness effect applied at the event (constant value or gamma draw).
struct CellMutation {
  uint32_t mutation_id{0};
  uint8_t mutation_type{0};
  float effect{0.0f};

  bool operator==(const CellMutation&) const = default;
};

class Cell {
 public:
  uint32_t parent_id{0};
  uint32_t id{0};
  float fitness{1.0};
  double death_time{0.0};

  // Realized mutation occurrences: parent id where the mutation was created,
  // mutation type id and the realized signed fitness effect.
  std::vector<CellMutation> mutations;

  Cell() = default;

  explicit Cell(uint32_t cellId) : id(cellId) {}

  explicit Cell(const Cell& parent, double cellFitness)
      : parent_id(parent.id),
        fitness(static_cast<float>(cellFitness)),
        mutations(parent.mutations) {}

  // Move constructor
  Cell(Cell&& other) noexcept
      : parent_id(other.parent_id),
        id(other.id),
        fitness(other.fitness),
        death_time(other.death_time),
        mutations(std::move(other.mutations)) {}

  // Copy constructor
  Cell(const Cell& other)
      : parent_id(other.parent_id),
        id(other.id),
        fitness(other.fitness),
        death_time(other.death_time),
        mutations(other.mutations) {}

  // Move assignment operator
  Cell& operator=(Cell&& other) noexcept {
    if (this != &other) {
      parent_id = other.parent_id;
      id = other.id;
      fitness = other.fitness;
      death_time = other.death_time;
      mutations = std::move(other.mutations);
    }
    return *this;
  }

  Cell& operator=(const Cell& other) = delete;
};

// Czy po śmierci starej komórki i narodzinach nowych tylko jedna
// dostaje nowy fitness z racji ewentualnej nowej mutacji
