#pragma once
#include <vector>
#include <memory>
#include <boost/pool/pool_alloc.hpp>

enum class MutationType : uint8_t {
    DRIVER = 0,
    POSITIVE = 1,
    NEUTRAL = 2,
    NEGATIVE = 3
};

inline std::ostream& operator<<(std::ostream& os, MutationType type) {
    switch (type) {
        case MutationType::DRIVER:
            os << "DRIVER";
            break;
        case MutationType::POSITIVE:
            os << "POSITIVE";
            break;
        case MutationType::NEUTRAL:
            os << "NEUTRAL";
            break;
        case MutationType::NEGATIVE:
            os << "NEGATIVE";
            break;
    }
    return os;
}
struct Mutation {
    double effect;
    double probability;
    uint32_t id;
    MutationType type;
};

class Cell {
public:
    //using CellAllocator = boost::pool_allocator<Cell>;
    uint32_t parent_id{0};
    uint32_t id{0};
    double fitness{1.0};
    double death_time{0.0};
    
    std::vector<uint8_t> mutations; 

    explicit Cell(uint32_t cellId) : id(cellId) {}

    
    explicit Cell(const Cell& parent, uint32_t cellId, double cellFitness) : 
        parent_id(parent.id), 
        id(cellId), 
        fitness(cellFitness), 
        mutations(parent.mutations) {}

    // Move constructor
    Cell(Cell&& other) noexcept :
        parent_id(other.parent_id),
        id(other.id),
        fitness(other.fitness),
        death_time(other.death_time),
        mutations(std::move(other.mutations)) {}
        
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