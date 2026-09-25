/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

#include <cassert>
#include <cmath>
#include <cstdint>

#include "gmansampling.h"
#include "ri.h"

namespace gman {

namespace {

// Salts separate the purposes sharing one hash chain -- a CMJ cell
// permutation, its two axis permutations, and the jitter for each --
// so no two draws from one pixel and dimension collide, and sample1D's
// draws stay independent of sample2D's at the same dimension.
constexpr std::uint32_t kSample1DPermPurpose = 0x9e3779b9u;
constexpr std::uint32_t kSample1DJitterSalt = 0x85ebca77u;
constexpr std::uint32_t kSample2DCellPurpose = 0xc2b2ae35u;
constexpr std::uint32_t kSample2DColPurpose = 0x27d4eb2fu;
constexpr std::uint32_t kSample2DRowPurpose = 0x165667b1u;
constexpr std::uint32_t kSample2DJitterXSalt = 0xd3a2646cu;
constexpr std::uint32_t kSample2DJitterYSalt = 0xfd7046c5u;

// Chris Wellons' lowbias32 32-bit integer finalizer -- full avalanche,
// low bias ("Prospecting for Hash Functions", nullprogram.com, 2018).
std::uint32_t lowbias32(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// One fold of a hash chain: order distinguishes a permuted tuple even
// though the fold itself (XOR) is symmetric, because the nonlinear mixer
// runs between folds.
std::uint32_t chain(std::uint32_t state, std::uint32_t value) { return lowbias32(state ^ value); }

// A hash for values that must stay fixed across every draw at one pixel
// and dimension -- a CMJ permutation seed -- so it folds in a purpose tag
// instead of a sample index.
std::uint32_t structuralHash(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t dimension, std::uint32_t purpose) {
  std::uint32_t h = lowbias32(seed);
  h = chain(h, static_cast<std::uint32_t>(x));
  h = chain(h, static_cast<std::uint32_t>(y));
  h = chain(h, dimension);
  h = chain(h, purpose);
  return h;
}

// The largest float below 1.0f. Adding a jittered stratum index and
// dividing by the stratum count is mathematically below 1, but float
// rounding can still land the sum on 1.0f when the jitter sits within
// about 2^-21 of 1 -- this nudges that one case back into range.
RtFloat clampBelowOne(RtFloat v) { return v < 1.0f ? v : std::nextafter(1.0f, 0.0f); }

// floor(sqrt(n)), exact: a float sqrt can round either side of an
// integer boundary, so this corrects it rather than trusting the cast.
std::uint32_t isqrtFloor(std::uint32_t n) {
  if (n == 0) {
    return 0;
  }
  std::uint32_t r = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(n)));
  while (r > 0 && r * r > n) {
    --r;
  }
  while ((r + 1) * (r + 1) <= n) {
    ++r;
  }
  return r;
}

// A bijective pseudo-random permutation of {0, ..., length-1}, by
// cycle-walking a bijection of the next power-of-two superset until it
// lands back in range -- the technique behind Kensler's own permute()
// ("Correlated Multi-Jittered Sampling"), with an independently chosen
// round function. Each step of permuteRound() is invertible (XOR by a
// fixed value, or multiply by an odd constant modulo the power-of-two
// domain), so their composition is a bijection on the masked domain;
// cycle-walking a bijection's orbit back into a subset of its domain is
// itself a bijection on that subset.
std::uint32_t permuteRound(std::uint32_t i, std::uint32_t mask, std::uint32_t seed) {
  i ^= seed & mask;
  i &= mask;
  i ^= i >> 16;
  i &= mask;
  i *= 0x7feb352du;
  i &= mask;
  i ^= i >> 8;
  i &= mask;
  i *= 0x846ca68bu;
  i &= mask;
  i ^= (seed >> 16) & mask;
  i &= mask;
  return i;
}

std::uint32_t permuteIndex(std::uint32_t index, std::uint32_t length, std::uint32_t seed) {
  if (length <= 1) {
    return 0;
  }
  std::uint32_t mask = length - 1;
  mask |= mask >> 1;
  mask |= mask >> 2;
  mask |= mask >> 4;
  mask |= mask >> 8;
  mask |= mask >> 16;

  std::uint32_t i = index;
  do {
    i = permuteRound(i, mask, seed);
  } while (i >= length);
  return i;
}

} // namespace

std::uint32_t sampleHash(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex, std::uint32_t dimension) {
  std::uint32_t h = lowbias32(seed);
  h = chain(h, static_cast<std::uint32_t>(x));
  h = chain(h, static_cast<std::uint32_t>(y));
  h = chain(h, sampleIndex);
  h = chain(h, dimension);
  return h;
}

RtFloat unitFloat(std::uint32_t h) { return static_cast<RtFloat>(h >> 8) * 0x1p-24f; }

RtFloat sample1D(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex, std::uint32_t sampleCount,
                 std::uint32_t dimension) {
  assert(sampleIndex < sampleCount);

  std::uint32_t const permSeed = structuralHash(seed, x, y, dimension, kSample1DPermPurpose);
  std::uint32_t const stratum = permuteIndex(sampleIndex, sampleCount, permSeed);
  std::uint32_t const jitterHash = chain(sampleHash(seed, x, y, sampleIndex, dimension), kSample1DJitterSalt);
  RtFloat const jitter = unitFloat(jitterHash);

  return clampBelowOne((static_cast<RtFloat>(stratum) + jitter) / static_cast<RtFloat>(sampleCount));
}

Sample2D sample2D(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex, std::uint32_t sampleCount,
                  std::uint32_t dimension) {
  assert(sampleIndex < sampleCount);

  std::uint32_t const m = isqrtFloor(sampleCount);
  std::uint32_t const n = (sampleCount + m - 1) / m;
  std::uint32_t const gridSize = m * n;

  // Shuffles which grid cell each sample index lands in, per pixel and
  // dimension, so the (cell, stratum) sequence itself -- not just its
  // jitter -- differs across pixels: composing a permutation with the
  // canonical index-to-cell split below is still a bijection onto the
  // grid.
  std::uint32_t const cellPermSeed = structuralHash(seed, x, y, dimension, kSample2DCellPurpose);
  std::uint32_t const s = permuteIndex(sampleIndex, sampleCount, cellPermSeed);
  std::uint32_t const i = s % m;
  std::uint32_t const j = s / m;

  // Within column i, the samples sharing it take each row 0..n-1
  // exactly once as sampleIndex ranges over sampleCount; permuting that
  // row by a seed keyed on i alone spreads them over every one of the
  // column's n sub-strata. The symmetric permutation on j spreads each
  // row over its m sub-strata.
  std::uint32_t const colPermSeed = structuralHash(seed, x, y, dimension, chain(kSample2DColPurpose, i));
  std::uint32_t const rowPermSeed = structuralHash(seed, x, y, dimension, chain(kSample2DRowPurpose, j));
  std::uint32_t const colSub = permuteIndex(j, n, colPermSeed);
  std::uint32_t const rowSub = permuteIndex(i, m, rowPermSeed);

  std::uint32_t const subColumn = i * n + colSub;
  std::uint32_t const subRow = j * m + rowSub;

  std::uint32_t const baseHash = sampleHash(seed, x, y, sampleIndex, dimension);
  RtFloat const jitterX = unitFloat(chain(baseHash, kSample2DJitterXSalt));
  RtFloat const jitterY = unitFloat(chain(baseHash, kSample2DJitterYSalt));

  RtFloat const u1 = clampBelowOne((static_cast<RtFloat>(subColumn) + jitterX) / static_cast<RtFloat>(gridSize));
  RtFloat const u2 = clampBelowOne((static_cast<RtFloat>(subRow) + jitterY) / static_cast<RtFloat>(gridSize));
  return {u1, u2};
}

} // namespace gman
