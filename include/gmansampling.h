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

#pragma once

#include <cstdint>

#include "gmanvector.h"
#include "ri.h"

/*
 * The renderer-facing sampling API: a counter-based sampler and the
 * direction warps it drives, public, installed and GMAN_EXPORT, so a
 * renderer plugin -- in this tree or outside it -- draws every random
 * number an integrator needs from one place.
 *
 * Every function here is a pure function of its arguments: nothing
 * carries state between calls, so an image reproduces however the caller
 * schedules its pixels and samples across tiles and workers.
 *
 * Warps take (u1, u2) in [0, 1)^2 and return a unit direction in a local
 * frame whose +z is the normal; a pdf is always per unit solid angle, so a
 * BSDF's and a light's combine under MIS with no conversion.
 */

namespace gman {

// One (u1, u2) draw.
struct GMAN_EXPORT Sample2D {
  RtFloat u1;
  RtFloat u2;
};

// One point in the plane, either a sample or a warp's output.
struct GMAN_EXPORT Point2D {
  RtFloat x;
  RtFloat y;
};

// Chains seed, pixel (x, y), sampleIndex and dimension through a 32-bit
// mixer in that order, so a permuted tuple -- x and y swapped, x and
// dimension swapped -- does not collide: changing any one input changes
// the output with overwhelming probability. The one source every other
// function here draws from.
GMAN_EXPORT std::uint32_t sampleHash(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex,
                                     std::uint32_t dimension);

// A hash's top 24 bits as a float in [0, 1): (h >> 8) * 2^-24, exactly
// representable for every h. h * 2^-32 would round to 1.0f for the top
// 128 hashes; this never does.
GMAN_EXPORT RtFloat unitFloat(std::uint32_t h);

// Sample sampleIndex of pixel (x, y)'s 1-D pattern of sampleCount values
// at dimension: a hashed permutation of sampleCount jittered strata,
// chosen by (seed, x, y, dimension), so the N values fill each of N equal
// strata of [0, 1) exactly once. sampleIndex < sampleCount is a
// precondition, checked by assert. A raw, unstratified draw stays
// reachable as unitFloat(sampleHash(...)).
GMAN_EXPORT RtFloat sample1D(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex, std::uint32_t sampleCount,
                             std::uint32_t dimension);

// Sample sampleIndex of pixel (x, y)'s correlated multi-jittered pattern
// of sampleCount points at dimension (Kensler, "Correlated
// Multi-Jittered Sampling", Pixar Technical Memo 13-01, 2013), chosen by
// (seed, x, y, dimension) and salted independent of sample1D at the same
// dimension. The grid is m = floor(sqrt(sampleCount)), n =
// ceil(sampleCount / m); when sampleCount == m * n, the sampleCount
// points fill every m*n grid cell once, every stratum of u1 once and
// every stratum of u2 once. sampleIndex < sampleCount is a precondition,
// checked by assert.
GMAN_EXPORT Sample2D sample2D(std::uint32_t seed, RtInt x, RtInt y, std::uint32_t sampleIndex,
                              std::uint32_t sampleCount, std::uint32_t dimension);

// Shirley and Chiu's concentric map: [0, 1)^2 onto the unit disk, uniform
// by area.
GMAN_EXPORT Point2D concentricDisk(RtFloat u1, RtFloat u2);

// Malley's method: concentricDisk lifted to the hemisphere by z = sqrt(max(0,
// 1 - x^2 - y^2)), so a rim point has z = 0. Preserves the disk's
// stratification.
GMAN_EXPORT GMANVector cosineHemisphere(RtFloat u1, RtFloat u2);

// Per unit solid angle: cosTheta / pi for cosTheta > 0, 0 otherwise.
GMAN_EXPORT RtFloat cosineHemispherePdf(RtFloat cosTheta);

GMAN_EXPORT GMANVector uniformHemisphere(RtFloat u1, RtFloat u2);
// Per unit solid angle: 1 / (2 * pi).
GMAN_EXPORT RtFloat uniformHemispherePdf();

GMAN_EXPORT GMANVector uniformSphere(RtFloat u1, RtFloat u2);
// Per unit solid angle: 1 / (4 * pi).
GMAN_EXPORT RtFloat uniformSpherePdf();

// A cone about +z of half-angle acos(cosThetaMax), cosThetaMax in
// [-1, 1); the returned direction has z >= cosThetaMax.
GMAN_EXPORT GMANVector uniformCone(RtFloat u1, RtFloat u2, RtFloat cosThetaMax);
// Per unit solid angle: 1 / (2 * pi * (1 - cosThetaMax)).
GMAN_EXPORT RtFloat uniformConePdf(RtFloat cosThetaMax);

// An orthonormal basis around a unit normal: t, b and n mutually
// orthogonal unit vectors with t.cross(b) == n. Built by Duff et al.'s
// branchless construction ("Building an Orthonormal Basis, Revisited",
// JCGT 6(1), 2017), which holds for every unit normal, including
// (0, 0, -1), where the usual cross-with-an-axis construction loses
// precision.
struct GMAN_EXPORT TangentFrame {
  GMANVector t, b, n;

  // v.x * t + v.y * b + v.z * n.
  GMANVector toWorld(GMANVector const& local) const;
  // The inverse of toWorld.
  GMANVector toLocal(GMANVector const& world) const;
};

GMAN_EXPORT TangentFrame tangentFrame(GMANVector const& n);

} // namespace gman
