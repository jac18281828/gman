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

#include <cmath>
#include <cstddef>
#include <vector>

#include "gmanmath.h"
#include "gmanpoint.h"

// Point coincidence and grouping: a sweep-friendly projection, a point's
// largest absolute coordinate, and path-compressed union-find over indices,
// shared by GMANRadiosityMesh's own coincident-node search and
// GMANRadiositySolver's node-indirect averaging.
namespace gman {

// The coincidence sweep's projection direction, (1, sqrt 2, sqrt 3) / sqrt
// 6: irrational ratios keep a regular grid's own projections apart, so a
// window near the tolerance holds little besides truly coincident points.
inline double const kNodeGroupSweepX = 1 / std::sqrt(6.0);
inline double const kNodeGroupSweepY = 1 / std::sqrt(3.0);
inline double const kNodeGroupSweepZ = 1 / std::sqrt(2.0);

inline double nodeGroupSweepProjection(GMANPoint const& p) {
  return kNodeGroupSweepX * (double)p.getX() + kNodeGroupSweepY * (double)p.getY() +
         kNodeGroupSweepZ * (double)p.getZ();
}

inline double maxAbsCoordinate(GMANPoint const& p) {
  return GMANMax(GMANMax(std::fabs((double)p.getX()), std::fabs((double)p.getY())), std::fabs((double)p.getZ()));
}

// The root of index's union-find set, compressing the path walked.
inline std::size_t findGroupRoot(std::vector<std::size_t>& parent, std::size_t index) {
  std::size_t root = index;
  while (parent[root] != root) {
    root = parent[root];
  }
  while (parent[index] != root) {
    std::size_t const next = parent[index];
    parent[index] = root;
    index = next;
  }
  return root;
}

// Joins two sets under the lower root, so every set's root is its lowest
// index.
inline void uniteGroups(std::vector<std::size_t>& parent, std::size_t a, std::size_t b) {
  std::size_t const rootA = findGroupRoot(parent, a);
  std::size_t const rootB = findGroupRoot(parent, b);
  if (rootA < rootB) {
    parent[rootB] = rootA;
  } else if (rootB < rootA) {
    parent[rootA] = rootB;
  }
}

} // namespace gman
