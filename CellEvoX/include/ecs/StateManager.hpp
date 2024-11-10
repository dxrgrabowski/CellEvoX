#pragma once
#include <rocksdb/db.h>
#include <memory>
#include "Cell.hpp"

class StateManager {
public:
    StateManager(bool use_rocksdb = false);
    
    void saveState(uint64_t step, const std::vector<Cell>& cells);
    std::vector<Cell> loadState(uint64_t step);
    
private:
    bool using_rocksdb;
    std::unique_ptr<rocksdb::DB> db;
    std::vector<std::vector<Cell>> in_memory_states;
};
