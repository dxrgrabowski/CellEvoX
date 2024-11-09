#pragma once
#include <rocksdb/db.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

class DatabaseManager {
public:
    struct SimulationRun {
        int id;
        std::string name;
        std::string simulation_type;
        int initial_population;
        double tau_step;
        std::chrono::system_clock::time_point timestamp;
        std::string status;
    };

    struct SimulationState {
        int run_id;
        int step;
        std::vector<uint8_t> state_data;
        double time;
    };

    DatabaseManager(const std::string& db_path = "/tmp/simulation_db");
    ~DatabaseManager();

    int createNewRun(const SimulationRun& run);
    void saveState(const SimulationState& state);
    std::vector<SimulationRun> getLastRuns(int limit = 10);
    SimulationState loadState(int run_id, int step);
    void updateRunStatus(int run_id, const std::string& status);

private:
    std::unique_ptr<rocksdb::DB> db;
    std::string serializeRun(const SimulationRun& run);
    SimulationRun deserializeRun(const std::string& data);
    std::string createKey(const std::string& prefix, int id);
};
