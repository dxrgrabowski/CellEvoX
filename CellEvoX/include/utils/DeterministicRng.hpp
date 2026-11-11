#pragma once

#include <cstdint>
#include <cmath>

namespace CellEvoX::deterministic_rng {

// Top-level RNG stream ids. These are part of the determinism contract:
// existing ids must never be reassigned (see docs/adr/0001-determinism.md).
constexpr uint64_t kStreamDeath = 0;
constexpr uint64_t kStreamBirth = 1;
constexpr uint64_t kStreamMutation = 2;
constexpr uint64_t kStreamMutationEffect = 3;

// Substream convention: a stream id below 2^32 is a top-level purpose, and
// `substream(base, slot)` moves to an independent slot of that purpose. The
// low 32 bits stay free for draw indices inside one substream, so adding a
// slot (e.g. a second mutation per division) never shifts existing draws.
constexpr uint64_t kSubstreamStride = 1ULL << 32U;

inline uint64_t substream(uint64_t base_stream, uint32_t slot) {
  return base_stream + static_cast<uint64_t>(slot) * kSubstreamStride;
}

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

// Standard normal from two uniforms (Box-Muller). Draw indices `stream` and
// `stream + 1` are consumed.
inline double normal01(uint64_t seed, uint64_t step, uint64_t cell_id, uint64_t stream) {
  constexpr double kTwoPi = 6.283185307179586476925286766559;
  const double u1 = uniform01(seed, step, cell_id, stream);
  const double u2 = uniform01(seed, step, cell_id, stream + 1);
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
}

// Gamma(shape, scale) via Marsaglia-Tsang (shape >= 1) and the U^(1/shape)
// boost (shape < 1). Counter-based and reproducible across standard libraries.
// Draw indices start at `stream` and advance by one per consumed uniform, so
// each substream owns a contiguous, non-overlapping draw range.
inline double gamma01(uint64_t seed, uint64_t step, uint64_t cell_id, uint64_t stream,
                      double shape, double scale) {
  if (shape < 1.0) {
    const double u = uniform01(seed, step, cell_id, stream);
    return gamma01(seed, step, cell_id, stream + 1, shape + 1.0, scale) *
           std::pow(u, 1.0 / shape);
  }
  const double d = shape - 1.0 / 3.0;
  const double c = 1.0 / std::sqrt(9.0 * d);
  uint64_t draw = stream;
  for (;;) {
    const double x = normal01(seed, step, cell_id, draw);
    draw += 2;
    const double v = 1.0 + c * x;
    if (v <= 0.0) continue;
    const double v3 = v * v * v;
    const double u = uniform01(seed, step, cell_id, draw);
    ++draw;
    if (std::log(u) < 0.5 * x * x + d * (1.0 - v3 + std::log(v3))) {
      return d * v3 * scale;
    }
  }
}

}  // namespace CellEvoX::deterministic_rng
