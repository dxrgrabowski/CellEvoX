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
struct Mutation {
    double effect;
    double probability;
    uint32_t id;
    MutationType type;
};

class Cell {
public:
    //using CellAllocator = boost::pool_allocator<Cell>;
    uint64_t parent_id{0};
    uint64_t id{0};
    double fitness{1.0};
    double birth_time{0.0};
    enum class State {
        ALIVE,
        DEAD,
    };
    State state{State::ALIVE};
    
    std::vector<Mutation> mutations; // Test later with boost::container::small_vector

    explicit Cell(uint64_t cellId) : id(cellId) {}

    explicit Cell(const Cell& parent, uint64_t cellId, double cellFitness, double birthTime) : 
        parent_id(parent.id), 
        id(cellId), 
        fitness(cellFitness), 
        mutations(parent.mutations),
        birth_time(birthTime) {}
};

// Czy przy podziale nowa komórka może dostać więcej niż jedną mutację? 
// Dlaczego, w ten sposób zaburzamy dane nam prawdopodobieństo mutacji i staje się to zależne od kolejności ifów