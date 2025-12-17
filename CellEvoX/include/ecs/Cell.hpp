#pragma once
#include <boost/pool/pool_alloc.hpp>
#include <memory>
#include <vector>

enum class MutationVariant : uint8_t { DRIVER = 0, POSITIVE = 1, NEUTRAL = 2, NEGATIVE = 3 };

inline std::ostream& operator<<(std::ostream& os, MutationVariant type) {
  switch (type) {
    case MutationVariant::DRIVER:
      os << "DRIVER";
      break;
    case MutationVariant::POSITIVE:
      os << "POSITIVE";
      break;
    case MutationVariant::NEUTRAL:
      os << "NEUTRAL";
      break;
    case MutationVariant::NEGATIVE:
      os << "NEGATIVE";
      break;
  }
  return os;
}
struct MutationType {
  double effect;
  double probability;
  uint8_t type_id;  // up to 256 mutationTypes
  MutationVariant type;
};

class Cell {
 public:
  // using CellAllocator = boost::pool_allocator<Cell>;
  uint32_t parent_id{0};
  uint32_t id{0};
  double fitness{1.0};
  double death_time{0.0};

  // <mutation_id> (id of parent where mutation was created), mutation_type_id>
  std::vector<std::pair<uint32_t, uint8_t>> mutations;

  explicit Cell(uint32_t cellId) : id(cellId) {}

  explicit Cell(const Cell& parent, double cellFitness)
      : parent_id(parent.id), fitness(cellFitness), mutations(parent.mutations) {}

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