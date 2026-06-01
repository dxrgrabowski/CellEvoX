#pragma once

#ifdef CELLEVOX_PROFILE_PHASES

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

namespace CellEvoX::profiling {

class PhaseProfiler {
 public:
  using Clock = std::chrono::steady_clock;

  static PhaseProfiler& instance() {
    static PhaseProfiler profiler;
    return profiler;
  }

  void record(std::string_view phase, Clock::duration duration) {
    if (!enabled_) {
      return;
    }

    const auto duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    std::lock_guard<std::mutex> lock(mutex_);
    output_ << phase << "," << duration_ns << "\n";
  }

 private:
  PhaseProfiler() {
    const char* env_value = std::getenv("CELLEVOX_PROFILE_PHASES");
    if (env_value == nullptr || env_value[0] == '\0') {
      return;
    }

    const std::string_view requested_path(env_value);
    if (requested_path == "0") {
      return;
    }

    const std::filesystem::path output_path =
        (requested_path == "1" || requested_path == "true" || requested_path == "TRUE" ||
         requested_path == "yes" || requested_path == "YES")
            ? std::filesystem::path("cellevox_phase_profile.csv")
            : std::filesystem::path(std::string(requested_path));

    const auto parent_path = output_path.parent_path();
    std::error_code ec;
    if (!parent_path.empty()) {
      std::filesystem::create_directories(parent_path, ec);
      if (ec) {
        return;
      }
    }

    bool write_header = true;
    if (std::filesystem::exists(output_path, ec)) {
      if (ec) {
        return;
      }
      const auto output_size = std::filesystem::file_size(output_path, ec);
      if (ec) {
        return;
      }
      write_header = output_size == 0;
    } else if (ec) {
      return;
    }

    output_.open(output_path, std::ios::out | std::ios::app);
    if (!output_.is_open()) {
      return;
    }

    enabled_ = true;
    if (write_header) {
      output_ << "phase,duration_ns\n";
    }
  }

  bool enabled_ = false;
  std::mutex mutex_;
  std::ofstream output_;
};

class ScopedPhase {
 public:
  explicit ScopedPhase(std::string_view phase) : phase_(phase) {
    (void)PhaseProfiler::instance();
    start_ = PhaseProfiler::Clock::now();
  }

  ScopedPhase(const ScopedPhase&) = delete;
  ScopedPhase& operator=(const ScopedPhase&) = delete;

  ~ScopedPhase() {
    PhaseProfiler::instance().record(phase_, PhaseProfiler::Clock::now() - start_);
  }

 private:
  std::string_view phase_;
  PhaseProfiler::Clock::time_point start_;
};

}  // namespace CellEvoX::profiling

#define CELLEVOX_PROFILE_CONCAT_IMPL(a, b) a##b
#define CELLEVOX_PROFILE_CONCAT(a, b) CELLEVOX_PROFILE_CONCAT_IMPL(a, b)
#define CELLEVOX_PROFILE_PHASE(name) \
  const ::CellEvoX::profiling::ScopedPhase CELLEVOX_PROFILE_CONCAT(cellevox_profile_phase_, __LINE__)(name)

#else

#define CELLEVOX_PROFILE_PHASE(name) ((void)0)

#endif
