#pragma once

#include <cstdint>
#include <cmath>
#include <stdexcept>

namespace CellEvoX::deterministic_rng {

// Top-level RNG stream ids. These are part of the determinism contract:
// existing ids must never be reassigned (see docs/adr/0001-determinism.md).
constexpr uint64_t kStreamDeath = 0;
constexpr uint64_t kStreamBirth = 1;
constexpr uint64_t kStreamMutation = 2;
constexpr uint64_t kStreamMutationEffect = 3;
// ADR-0013: Poisson mutation count and Poisson type selection. The legacy
// gamma-effect stream starts at 3 and walks forward one key per draw, so these
// purposes live far above that range: a low id such as 4/5 would alias the
// effect sampler's draw indices (and each other) and correlate the count with
// the selected type. High, sparse keys keep every purpose disjoint while
// leaving the legacy 0/1/2/3 sequence bit-identical.
constexpr uint64_t kStreamMutationCount = 1ULL << 56U;
constexpr uint64_t kStreamMutationType = 1ULL << 57U;

// Substream convention: a top-level purpose owns a base stream id, and
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

// A double below 2^64 is representable as uint64_t; a sample at or above it
// would make the conversion undefined.
inline uint64_t poissonSampleToUint64(double k) {
  constexpr double kUint64Range = 18446744073709551616.0;  // 2^64
  if (!(k >= 0.0) || k >= kUint64Range) {
    throw std::overflow_error("poisson01: sample is not representable as uint64_t");
  }
  return static_cast<uint64_t>(k);
}

// Poisson(lambda) draw. Counter-based and reproducible across standard
// libraries: small rates use inversion by sequential search (one uniform),
// larger rates use the transformed rejection method (Hörmann PTRS), which
// consumes two uniforms per attempt. `stream` is the first draw index; the
// PTRS path advances it by two per attempt so each substream owns a
// contiguous, non-overlapping draw range. No cap is applied; the caller is
// responsible for keeping `lambda` finite and non-negative.
inline uint64_t poisson01(uint64_t seed, uint64_t step, uint64_t cell_id, uint64_t stream,
                          double lambda) {
  if (!(lambda > 0.0)) {
    return 0;
  }
  constexpr double kUint64Range = 18446744073709551616.0;  // 2^64
  if (lambda >= kUint64Range) {
    throw std::overflow_error("poisson01: lambda is not representable as uint64_t");
  }
  if (lambda < 10.0) {
    // uniform01 can round up to exactly 1.0, but the Poisson CDF is strictly
    // below 1, so an unclamped 1.0 would never satisfy the loop condition.
    double u = uniform01(seed, step, cell_id, stream);
    if (u >= 1.0) {
      u = std::nextafter(1.0, 0.0);
    }
    double term = std::exp(-lambda);
    double cumulative = term;
    uint64_t k = 0;
    while (u > cumulative) {
      ++k;
      term *= lambda / static_cast<double>(k);
      const double next = cumulative + term;
      if (next == cumulative) {
        // The remaining tail is below one ulp of the running sum, so no later
        // term can advance it; stop instead of spinning forever.
        break;
      }
      cumulative = next;
    }
    return k;
  }
  const double slam = std::sqrt(lambda);
  const double loglam = std::log(lambda);
  const double b = 0.931 + 2.53 * slam;
  const double a = -0.059 + 0.02483 * b;
  const double invalpha = 1.1239 + 1.1328 / (b - 3.4);
  const double vr = 0.9277 - 3.6224 / (b - 2.0);
  uint64_t draw = stream;
  for (;;) {
    const double u = uniform01(seed, step, cell_id, draw) - 0.5;
    const double v = uniform01(seed, step, cell_id, draw + 1);
    draw += 2;
    const double us = 0.5 - std::fabs(u);
    const double k = std::floor((2.0 * a / us + b) * u + lambda + 0.43);
    if (us >= 0.07 && v <= vr) {
      return poissonSampleToUint64(k);
    }
    if (k < 0.0 || (us < 0.013 && v > us)) {
      continue;
    }
    if (std::log(v) + std::log(invalpha) - std::log(a / (us * us) + b) <=
        -lambda + k * loglam - std::lgamma(k + 1.0)) {
      return poissonSampleToUint64(k);
    }
  }
}

}  // namespace CellEvoX::deterministic_rng
