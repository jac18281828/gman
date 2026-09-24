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
#include <cmath>

#include "gmanmath.h"
#include "gmanrayquadratic.h"
#include "gmanrayquartic.h"

namespace {

// The relative threshold, against |p|, below which the resolvent cubic's
// linear-term estimate of m is treated as small enough to reseed from
// directly (see refineResolventRoot): m scales as p, so this is a scale-
// free fraction of p rather than an absolute floor.
constexpr double kResolventSeedFloor = 1e-8;

// The largest real root of the depressed cubic n^3 + aa*n + bb == 0,
// Cardano's method with the trigonometric form when it has three real
// roots. Ferrari's resolvent cubic always has a root with the sign this
// caller needs among its real roots, so the largest one is always it.
double largestRealCubicRoot(double aa, double bb) {
  double const disc = (bb * bb / 4.0) + (aa * aa * aa / 27.0);
  if (disc > 0.0) {
    double const sq = std::sqrt(disc);
    return std::cbrt(-bb / 2.0 + sq) + std::cbrt(-bb / 2.0 - sq);
  }
  if (aa == 0.0) // disc <= 0 and aa == 0 forces bb == 0 too: a triple root at 0.
    return 0.0;

  double const radius = 2.0 * std::sqrt(-aa / 3.0);
  double const arg = GMANClamp((3.0 * bb) / (2.0 * aa) * std::sqrt(-3.0 / aa), -1.0, 1.0);
  double const theta = std::acos(arg) / 3.0;

  double largest = -radius; // a lower bound: every root has magnitude <= radius.
  for (int k = 0; k < 3; ++k) {
    double const root = radius * std::cos(theta - 2.0 * PI * k / 3.0);
    largest = std::max(largest, root);
  }
  return largest;
}

// The biquadratic special case (the original quartic's own b == d == 0,
// so q == 0 after depression): y^4 + p*y^2 + r == 0, a quadratic in
// w == y^2, with no resolvent cubic to solve. Writes up to 2 real roots
// into yRoots and returns their count.
int solveBiquadratic(double p, double r, double yRoots[2]) {
  double w0 = 0.0, w1 = 0.0;
  int const nW = gman::solveRayQuadratic(1.0, p, r, w0, w1);
  double const w[2] = {w0, w1};
  int nY = 0;
  for (int i = 0; i < nW; ++i) {
    if (w[i] < 0.0)
      continue;
    double const s = std::sqrt(w[i]);
    yRoots[nY++] = -s;
    yRoots[nY++] = s;
  }
  return nY;
}

// Refines seed -- the depressed cubic's own root n minus its p/3 shift --
// against the exact resolvent cubic 8m^3+8p*m^2+(2p^2-8r)*m-q^2 == 0.
// Reseeds from q^2/linear (linear == 2p^2-8r) whenever that estimate sits
// below kResolventSeedFloor * |p|: there, n - p/3 is cancellation noise
// rather than a measurement, while 8m^3 and 8p*m^2 are negligible next to
// the linear term, leaving (2p^2-8r)*m ~= q^2. Each Newton step is
// accepted only if it shrinks the residual, so a seed already at or past
// the nearby root simply stops.
double refineResolventRoot(double p, double r, double q, double seed) {
  double const linear = 2.0 * p * p - 8.0 * r;
  double m = seed;
  if (linear > 0.0 && (q * q) / linear < kResolventSeedFloor * std::abs(p))
    m = (q * q) / linear;

  double f = 8.0 * m * m * m + 8.0 * p * m * m + linear * m - q * q;
  for (int iter = 0; iter < 8; ++iter) {
    double const fp = 24.0 * m * m + 16.0 * p * m + linear;
    if (fp == 0.0)
      break;
    double const delta = f / fp;
    double const mNext = m - delta;
    double const fNext = 8.0 * mNext * mNext * mNext + 8.0 * p * mNext * mNext + linear * mNext - q * q;
    if (std::abs(fNext) >= std::abs(f))
      break;
    m = mNext;
    f = fNext;
    if (std::abs(delta) <= 1e-15 * std::max(1.0, std::abs(m)))
      break;
  }
  return m;
}

// Ferrari's method, q != 0: pick m solving the resolvent cubic so that
// y^4+p*y^2+q*y+r == (y^2+p/2+m)^2 - (sqrt(2m)*y - q/(2*sqrt(2m)))^2,
// factoring the quartic into two quadratics in y. Writes up to 4 real
// roots (unsorted) into yRoots and returns their count.
int solveFerrari(double p, double q, double r, double yRoots[4]) {
  double const cubicQ = p * p / 4.0 - r;
  double const cubicR = -q * q / 8.0;
  double const aa = cubicQ - p * p / 3.0;
  double const cubicBB = 2.0 * p * p * p / 27.0 - p * cubicQ / 3.0 + cubicR;
  double const n = largestRealCubicRoot(aa, cubicBB);
  double const m = std::max(refineResolventRoot(p, r, q, n - p / 3.0), 0.0);

  double const sqrt2m = std::sqrt(2.0 * m);
  // m == 0 forces q == 0 by the resolvent cubic itself (ruled out above),
  // so this branch never divides by an exact zero.
  double const qTerm = sqrt2m > 0.0 ? q / (2.0 * sqrt2m) : 0.0;

  int nY = 0;
  double y0 = 0.0, y1 = 0.0;
  int const n1 = gman::solveRayQuadratic(1.0, -sqrt2m, p / 2.0 + m + qTerm, y0, y1);
  if (n1 >= 1)
    yRoots[nY++] = y0;
  if (n1 >= 2)
    yRoots[nY++] = y1;

  double y2 = 0.0, y3 = 0.0;
  int const n2 = gman::solveRayQuadratic(1.0, sqrt2m, p / 2.0 + m - qTerm, y2, y3);
  if (n2 >= 1)
    yRoots[nY++] = y2;
  if (n2 >= 2)
    yRoots[nY++] = y3;
  return nY;
}

} // namespace

namespace gman {

int solveQuartic(double a, double b, double c, double d, double e, double roots[4]) {
  double const bb = b / a, cc = c / a, dd = d / a, ee = e / a;

  // Depress: t == y - bb/4 removes the cubic term, leaving
  // y^4 + p*y^2 + q*y + r == 0.
  double const p = cc - 3.0 * bb * bb / 8.0;
  double const q = bb * bb * bb / 8.0 - bb * cc / 2.0 + dd;
  double const r = -3.0 * bb * bb * bb * bb / 256.0 + bb * bb * cc / 16.0 - bb * dd / 4.0 + ee;
  double const shift = -bb / 4.0;

  double yRoots[4];
  int const nY = (q == 0.0) ? solveBiquadratic(p, r, yRoots) : solveFerrari(p, q, r, yRoots);

  int nT = 0;
  for (int i = 0; i < nY; ++i)
    roots[nT++] = yRoots[i] + shift;

  std::sort(roots, roots + nT);
  return nT;
}

} // namespace gman
