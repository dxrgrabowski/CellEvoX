#pragma once
#include <vector>
#include <memory>
#include <boost/pool/pool_alloc.hpp>

struct Mutation {
    double effect;
    double probability;
    uint32_t id;
};

class Cell {
public:
    using CellAllocator = boost::pool_allocator<Cell>;
    
    uint64_t id;
    std::vector<Mutation> mutations;
    double fitness;
    
    enum class State {
        ALIVE,
        DEAD,
        ALIVE_SPLITTING
    };
    
    State state;
};
