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

#include "gmanmath.h"

namespace gman {

// Solves a ray's own a*t^2 + b*t + c == 0 in double, shared by the
// sphere and the four swept quadrics' intersectors (cylinder, cone,
// hyperboloid, paraboloid), each from an origin shifted into the shape's
// bounding sphere. a vanishes when the ray's direction cancels the
// quadratic term -- parallel to a generatrix or a ruling, or down an
// axis -- leaving at most a linear equation: b == 0 too has no equation
// left to solve, a miss; otherwise the single root is -c/b. A nonzero a
// solves in the stable q form itself, rather than delegating to
// GMANQuadraticRoots (public, RtFloat only): a negative discriminant
// returns no roots, a zero discriminant returns one, and two roots come
// back ascending, the smaller one recovered through c/q rather than a
// cancelling subtraction. A merely near-zero a needs no separate case:
// q's sign matches b's, so q itself never cancels, and c/q already
// converges to -c/b, the linear root, as a shrinks toward it.
inline int solveRayQuadratic(double a, double b, double c, double& t0, double& t1) {
  if (a == 0.0) {
    if (b == 0.0)
      return 0;
    t0 = -c / b;
    return 1;
  }

  double const disc = b * b - 4.0 * a * c;
  if (disc < 0.0)
    return 0;
  if (disc == 0.0) {
    t0 = -b / (2.0 * a);
    return 1;
  }

  double const sqrtDisc = std::sqrt(disc);
  double const sign = b < 0.0 ? -1.0 : 1.0;
  double const q = -(b + sign * sqrtDisc) / 2.0;

  double const r0 = q / a;
  double const r1 = c / q;
  t0 = GMANMin(r0, r1);
  t1 = GMANMax(r0, r1);
  return 2;
}

} // namespace gman
