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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "gmansampling.h"
#include "ri.h"

namespace gman {

namespace {

// Folded into every hash chain's initial state so the all-zero tuple does
// not hash to 0 (lowbias32(0) == 0).
constexpr std::uint32_t kChainInit = 0x9e3779b9u;

// Folded into structuralHash's initial state only: sampleHash never
// mixes this in, so no dimension a renderer draws lets a sampleHash
// draw reproduce a permutation seed. Reproducing one needs a dimension
// equal to one of the salts below, not a value any renderer passes.
constexpr std::uint32_t kStructuralDomain = 0x85ebca6bu;

// Salts separate the purposes sharing one hash chain, so no two draws
// from one pixel and dimension collide, and sample1D's draws stay
// independent of sample2D's at the same dimension.
constexpr std::uint32_t kSample1DPermSalt = 0xc2b2ae35u;
constexpr std::uint32_t kSample1DJitterSalt = 0x27d4eb2fu;
constexpr std::uint32_t kSample2DPatternSalt = 0x165667b1u;
constexpr std::uint32_t kSample2DJitterXSalt = 0xd3a2646cu;
constexpr std::uint32_t kSample2DJitterYSalt = 0xfd7046c5u;

// Kensler's cmj salts, multiplied into the pattern seed to separate the
// index permutation from the column and row shuffles.
constexpr std::uint32_t kCmjIndexSalt = 0x51633e2du;
constexpr std::uint32_t kCmjColSalt = 0xa511e9b3u;
constexpr std::uint32_t kCmjRowSalt = 0x63d83595u;

constexpr RtFloat kPi = 3.14159265358979323846f;
constexpr RtFloat kInvPi = 1.0f / kPi;
constexpr RtFloat kInv2Pi = 1.0f / (2.0f * kPi);
constexpr RtFloat kInv4Pi = 1.0f / (4.0f * kPi);
constexpr RtFloat kPiOver4 = kPi / 4.0f;
constexpr RtFloat kPiOver2 = kPi / 2.0f;

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
// and dimension -- a CMJ pattern seed -- so it folds in a purpose salt
// instead of a sample index, from an initial state sampleHash never
// reaches.
std::uint32_t structuralHash(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t dimension, std::uint32_t salt) {
  std::uint32_t h = lowbias32(seed ^ kChainInit ^ kStructuralDomain);
  h = chain(h, static_cast<std::uint32_t>(x));
  h = chain(h, static_cast<std::uint32_t>(y));
  h = chain(h, dimension);
  h = chain(h, salt);
  return h;
}

// The largest float below 1.0f. (stratum + jitter) / count is
// mathematically below 1, but float rounding can still land the sum on
// 1.0f: the rounding window is about ulp(count)/2 near 1, growing with
// count, not a fixed fraction.
RtFloat clampBelowOne(RtFloat v) { return v < 1.0f ? v : std::nextafter(1.0f, 0.0f); }

// floor(sqrt(n)), exact, computed in 64 bits: a float sqrt can round
// either side of an integer boundary, and (r+1)*(r+1) would overflow 32
// bits for n near UINT32_MAX.
std::uint32_t isqrtFloor(std::uint32_t n) {
  if (n == 0) {
    return 0;
  }
  auto r = static_cast<std::uint64_t>(std::sqrt(static_cast<double>(n)));
  std::uint64_t const n64 = n;
  while (r > 0 && r * r > n64) {
    --r;
  }
  while ((r + 1) * (r + 1) <= n64) {
    ++r;
  }
  return static_cast<std::uint32_t>(r);
}

// A sampleCount of 0 behaves as 1, before any division, in both
// samplers: a zero-sample request then yields a defined sample instead
// of a trap in a release build.
std::uint32_t zeroCountAsOne(std::uint32_t sampleCount) { return sampleCount == 0 ? 1u : sampleCount; }

// Kensler's permute: a pseudo-random bijection of {0, ..., length-1} by
// cycle-walking a bijection of the smallest power-of-two superset back
// into range (Kensler, "Correlated Multi-Jittered Sampling", Pixar
// Technical Memo 13-01, 2013). The mask is built by OR-shift, not
// std::bit_ceil: length can reach UINT32_MAX, where the next power of
// two overflows std::bit_ceil's domain.
std::uint32_t permute(std::uint32_t i, std::uint32_t length, std::uint32_t seed) {
  std::uint32_t w = length - 1u;
  w |= w >> 1;
  w |= w >> 2;
  w |= w >> 4;
  w |= w >> 8;
  w |= w >> 16;
  do {
    i ^= seed;
    i *= 0xe170893du;
    i ^= seed >> 16;
    i ^= (i & w) >> 4;
    i ^= seed >> 8;
    i *= 0x0929eb3fu;
    i ^= seed >> 23;
    i ^= (i & w) >> 1;
    i *= 1u | seed >> 27;
    i *= 0x6935fa69u;
    i ^= (i & w) >> 11;
    i *= 0x74dcb303u;
    i ^= (i & w) >> 2;
    i *= 0x9e501cc3u;
    i ^= (i & w) >> 2;
    i *= 0xc860a3dfu;
    i &= w;
    i ^= i >> 5;
  } while (i >= length);
  return (i + seed) % length;
}

} // namespace

std::uint32_t sampleHash(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex, std::uint32_t dimension) {
  std::uint32_t h = lowbias32(seed ^ kChainInit);
  h = chain(h, static_cast<std::uint32_t>(x));
  h = chain(h, static_cast<std::uint32_t>(y));
  h = chain(h, sampleIndex);
  h = chain(h, dimension);
  return h;
}

RtFloat unitFloat(std::uint32_t h) { return static_cast<RtFloat>(h >> 8) * 0x1p-24f; }

RtFloat sample1D(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex, std::uint32_t sampleCount,
                 std::uint32_t dimension) {
  std::uint32_t const count = zeroCountAsOne(sampleCount);
  assert(sampleIndex < count);

  std::uint32_t const permSeed = structuralHash(seed, x, y, dimension, kSample1DPermSalt);
  std::uint32_t const stratum = permute(sampleIndex, count, permSeed);
  std::uint32_t const jitterHash = chain(sampleHash(seed, x, y, sampleIndex, dimension), kSample1DJitterSalt);
  RtFloat const jitter = unitFloat(jitterHash);

  return clampBelowOne((static_cast<RtFloat>(stratum) + jitter) / static_cast<RtFloat>(count));
}

Sample2D sample2D(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex, std::uint32_t sampleCount,
                  std::uint32_t dimension) {
  std::uint32_t const count = zeroCountAsOne(sampleCount);
  assert(sampleIndex < count);

  std::uint32_t const m = isqrtFloor(count);
  // 64 bits: count + m - 1 overflows 32 bits when count is near
  // UINT32_MAX, which would silently floor n to 0 and send permute()
  // into an unbounded cycle walk over an empty range.
  std::uint32_t const n =
      static_cast<std::uint32_t>((static_cast<std::uint64_t>(count) + m - 1) / static_cast<std::uint64_t>(m));

  // One column shuffle and one row shuffle per pattern (seed, x, y,
  // dimension), shared across every sample index: Kensler's cmj,
  // correlated multi-jittered sampling.
  std::uint32_t const pattern = structuralHash(seed, x, y, dimension, kSample2DPatternSalt);
  std::uint32_t const s = permute(sampleIndex, count, pattern * kCmjIndexSalt);
  std::uint32_t const i = s % m;
  std::uint32_t const j = s / m;
  std::uint32_t const colShuffle = permute(i, m, pattern * kCmjColSalt);
  std::uint32_t const rowShuffle = permute(j, n, pattern * kCmjRowSalt);

  std::uint32_t const baseHash = sampleHash(seed, x, y, sampleIndex, dimension);
  RtFloat const jitterX = unitFloat(chain(baseHash, kSample2DJitterXSalt));
  RtFloat const jitterY = unitFloat(chain(baseHash, kSample2DJitterYSalt));

  RtFloat const u1 =
      clampBelowOne((static_cast<RtFloat>(i) + (static_cast<RtFloat>(rowShuffle) + jitterX) / static_cast<RtFloat>(n)) /
                    static_cast<RtFloat>(m));
  RtFloat const u2 =
      clampBelowOne((static_cast<RtFloat>(j) + (static_cast<RtFloat>(colShuffle) + jitterY) / static_cast<RtFloat>(m)) /
                    static_cast<RtFloat>(n));
  return {u1, u2};
}

Point2D concentricDisk(RtFloat u1, RtFloat u2) {
  RtFloat const ox = 2.0f * u1 - 1.0f;
  RtFloat const oy = 2.0f * u2 - 1.0f;
  if (ox == 0.0f && oy == 0.0f) {
    return {0.0f, 0.0f};
  }

  RtFloat r;
  RtFloat theta;
  if (std::fabs(ox) > std::fabs(oy)) {
    r = ox;
    theta = kPiOver4 * (oy / ox);
  } else {
    r = oy;
    theta = kPiOver2 - kPiOver4 * (ox / oy);
  }
  return {r * std::cos(theta), r * std::sin(theta)};
}

GMANVector cosineHemisphere(RtFloat u1, RtFloat u2) {
  Point2D const d = concentricDisk(u1, u2);
  RtFloat const z = std::sqrt(std::max(0.0f, 1.0f - d.x * d.x - d.y * d.y));
  return GMANVector(d.x, d.y, z);
}

RtFloat cosineHemispherePdf(RtFloat cosTheta) { return cosTheta > 0.0f ? cosTheta * kInvPi : 0.0f; }

GMANVector uniformHemisphere(RtFloat u1, RtFloat u2) {
  RtFloat const z = u1;
  RtFloat const r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  RtFloat const phi = 2.0f * kPi * u2;
  return GMANVector(r * std::cos(phi), r * std::sin(phi), z);
}

RtFloat uniformHemispherePdf() { return kInv2Pi; }

GMANVector uniformSphere(RtFloat u1, RtFloat u2) {
  RtFloat const z = 1.0f - 2.0f * u1;
  RtFloat const r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  RtFloat const phi = 2.0f * kPi * u2;
  return GMANVector(r * std::cos(phi), r * std::sin(phi), z);
}

RtFloat uniformSpherePdf() { return kInv4Pi; }

GMANVector uniformCone(RtFloat u1, RtFloat u2, RtFloat cosThetaMax) {
  RtFloat const cosTheta = (1.0f - u1) + u1 * cosThetaMax;
  RtFloat const sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
  RtFloat const phi = 2.0f * kPi * u2;
  return GMANVector(sinTheta * std::cos(phi), sinTheta * std::sin(phi), std::max(cosTheta, cosThetaMax));
}

RtFloat uniformConePdf(RtFloat cosThetaMax) { return kInv2Pi / (1.0f - cosThetaMax); }

GMANVector TangentFrame::toWorld(GMANVector const& local) const {
  return t * local.getX() + b * local.getY() + n * local.getZ();
}

GMANVector TangentFrame::toLocal(GMANVector const& world) const {
  return GMANVector(t.dot(world), b.dot(world), n.dot(world));
}

TangentFrame tangentFrame(GMANVector const& n) {
  RtFloat const sign = std::copysign(1.0f, n.getZ());
  RtFloat const a = -1.0f / (sign + n.getZ());
  RtFloat const b = n.getX() * n.getY() * a;

  GMANVector const t(1.0f + sign * n.getX() * n.getX() * a, sign * b, -sign * n.getX());
  GMANVector const bt(b, sign + n.getY() * n.getY() * a, -n.getY());
  return {t, bt, n};
}

} // namespace gman
