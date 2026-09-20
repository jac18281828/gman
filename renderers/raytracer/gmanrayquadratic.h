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

#include "gmanmath.h"

// Solves a ray's own a*t^2 + b*t + c == 0, shared by the swept quadrics'
// intersectors (cone, cylinder, hyperboloid, paraboloid). a vanishes when
// the ray's direction cancels the quadratic term -- parallel to a
// generatrix or a ruling, or down an axis -- leaving at most a linear
// equation: b == 0 too has no equation left to solve, a miss; otherwise
// the single root is -c/b. That exactly-degenerate a is reachable in
// principle; a merely near-degenerate one never reaches this branch,
// since GMANQuadraticRoots' stable c/q form already recovers its correct
// root. A nonzero a delegates to it unchanged.
inline int solveRayQuadratic(RtFloat a, RtFloat b, RtFloat c, RtFloat& t0, RtFloat& t1) {
  if (a == 0.0) {
    if (b == 0.0)
      return 0;
    t0 = -c / b;
    return 1;
  }
  return GMANQuadraticRoots(a, b, c, t0, t1);
}
