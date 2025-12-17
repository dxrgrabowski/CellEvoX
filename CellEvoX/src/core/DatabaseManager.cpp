#include "core/DatabaseManager.hpp"

#include <rocksdb/options.h>
#include <rocksdb/slice.h>

#include <sstream>

DatabaseManager::DatabaseManager(const std::string& db_path) {
  rocksdb::Options options;
  options.create_if_missing = true;
  options.IncreaseParallelism();
  options.OptimizeLevelStyleCompaction();

  rocksdb::DB* db_ptr;
  rocksdb::Status status = rocksdb::DB::Open(options, db_path, &db_ptr);
  if (!status.ok()) {
    throw std::runtime_error("Failed to open database: " + status.ToString());
  }
  db.reset(db_ptr);
}

int DatabaseManager::createNewRun(const SimulationRun& run) {
  int new_id = 0;
  std::string last_id_str;
  auto status = db->Get(rocksdb::ReadOptions(), "last_run_id", &last_id_str);

  if (status.ok()) {
    new_id = std::stoi(last_id_str) + 1;
  }

  std::string serialized = serializeRun(run);
  status = db->Put(rocksdb::WriteOptions(), createKey("run", new_id), serialized);

  if (status.ok()) {
    db->Put(rocksdb::WriteOptions(), "last_run_id", std::to_string(new_id));
    return new_id;
  }

  throw std::runtime_error("Failed to create new run");
}

void DatabaseManager::saveState(const SimulationState& state) {
  std::string key = createKey("state", state.run_id) + "_" + std::to_string(state.step);
  rocksdb::Slice value(reinterpret_cast<const char*>(state.state_data.data()),
                       state.state_data.size());

  auto status = db->Put(rocksdb::WriteOptions(), key, value);
  if (!status.ok()) {
    throw std::runtime_error("Failed to save state");
  }
}

std::vector<DatabaseManager::SimulationRun> DatabaseManager::getLastRuns(int limit) {
  std::vector<SimulationRun> runs;
  std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(rocksdb::ReadOptions()));

  for (it->SeekToLast(); it->Valid() && runs.size() < limit; it->Prev()) {
    if (it->key().ToString().substr(0, 3) == "run") {
      runs.push_back(deserializeRun(it->value().ToString()));
    }
  }

  return runs;
}

std::string DatabaseManager::createKey(const std::string& prefix, int id) {
  return prefix + "_" + std::to_string(id);
}
