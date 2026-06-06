#pragma once

#include <cstdint>
#include <cmath>

namespace CellEvoX::deterministic_rng {

inline uint64_t splitMix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

inline uint64_t mix(uint64_t seed, uint64_t step, uint64_t cell_id, uint64_t stream) {
  uint64_t value = seed;
  value ^= splitMix64(step + 0x632be59bd9b4e019ULL);
  value ^= splitMix64(cell_id + 0x85157af5ULL);
  value ^= splitMix64(stream + 0x94d049bb133111ebULL);
  return splitMix64(value);
}

inline double uniform01(uint64_t seed, uint64_t step, uint64_t cell_id, uint64_t stream) {
  constexpr double denominator = 9007199254740992.0;  // 2^53
  const uint64_t bits = mix(seed, step, cell_id, stream) >> 11U;
  return (static_cast<double>(bits) + 0.5) / denominator;
}

inline double exponential01(uint64_t seed, uint64_t step, uint64_t cell_id, uint64_t stream) {
  const double u = uniform01(seed, step, cell_id, stream);
  return -std::log1p(-u);
}

}  // namespace CellEvoX::deterministic_rng
