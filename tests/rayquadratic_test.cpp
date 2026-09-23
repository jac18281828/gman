/* SPDX-License-Identifier: LGPL-2.1-or-later
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

/*
 * R5c review proof: gman::solveRayQuadratic solves the a == 0 linear case
 * exactly and, for a nonzero, implements the stable q form itself in
 * double: a negative discriminant returns no roots, a zero discriminant
 * returns one, and two roots come back ascending, the smaller one to full
 * precision even when b*b >> 4*a*c.
 */

#include <cmath>

#include "check.h"
#include "gmanrayquadratic.h"

namespace {

// ---- check 1: a == 0, b != 0 is the single root -c/b ----
void testLinearRoot() {
  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(0.0, 2.0, -6.0, t0, t1);

  check(numRoots == 1, "linear: a == 0, b != 0 returns one root");
  check(t0 == 3.0, "linear: -c/b == 3");
}

// ---- check 2: a == 0, b == 0 has no equation left to solve ----
void testLinearNoEquation() {
  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(0.0, 0.0, 1.0, t0, t1);

  check(numRoots == 0, "linear: a == 0, b == 0 returns no roots");
}

// ---- check 3: a negative discriminant returns no roots ----
void testNegativeDiscriminant() {
  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(1.0, 0.0, 1.0, t0, t1); // t^2 + 1 == 0

  check(numRoots == 0, "quadratic: negative discriminant returns no roots");
}

// ---- check 4: a zero discriminant returns the one repeated root ----
void testZeroDiscriminant() {
  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(1.0, -4.0, 4.0, t0, t1); // (t - 2)^2 == 0

  check(numRoots == 1, "quadratic: zero discriminant returns one root");
  check(t0 == 2.0, "quadratic: the repeated root is 2");
}

// ---- check 5: two roots come back ascending ----
void testAscendingRoots() {
  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(1.0, -3.0, 2.0, t0, t1); // (t - 1)(t - 2) == 0

  check(numRoots == 2, "quadratic: two real roots");
  check(t0 == 1.0 && t1 == 2.0, "quadratic: roots come back ascending, 1 then 2");
}

// ---- check 6: b*b >> 4*a*c keeps the small root to full precision ----
// Roots at 1e-9 and 1e9 (a == 1, b == -1e9, c == 1): the naive
// (-b +/- sqrt(disc))/(2a) reaches the small root by subtracting two
// nearly equal ~1e9 magnitude values, losing most of its digits to
// cancellation. The stable q form reaches it through c/q, a division,
// instead, so t0*t1 == c/a holds to near machine precision.
void testCancellationResistantSmallRoot() {
  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(1.0, -1e9, 1.0, t0, t1);

  check(numRoots == 2, "quadratic: b*b >> 4*a*c still returns two roots");
  check(t0 > 0.0 && t0 < 1.0, "quadratic: the small root stays small, not cancellation noise");
  check(std::fabs(t0 * t1 - 1.0) < 1e-6,
        "quadratic: t0*t1 == c/a to near machine precision, the small root's digits intact");
}

} // namespace

int main() {
  testLinearRoot();
  testLinearNoEquation();
  testNegativeDiscriminant();
  testZeroDiscriminant();
  testAscendingRoots();
  testCancellationResistantSmallRoot();

  return checkSummary("solveRayQuadratic solves the linear case exactly and the quadratic case stably");
}
