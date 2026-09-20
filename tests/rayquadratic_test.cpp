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
 * R5b review proof: solveRayQuadratic solves the a == 0 linear case
 * exactly, at coefficients a caller's own floating-point contraction
 * cannot perturb, and otherwise delegates to GMANQuadraticRoots unchanged.
 */

#include "check.h"
#include "gmanrayquadratic.h"

namespace {

// ---- check 1: a == 0, b != 0 is the single root -c/b ----
void testLinearRoot() {
  RtFloat t0 = 0.0, t1 = 0.0;
  int const numRoots = solveRayQuadratic(0.0, 2.0, -6.0, t0, t1);

  check(numRoots == 1, "linear: a == 0, b != 0 returns one root");
  check(t0 == 3.0, "linear: -c/b == 3");
}

// ---- check 2: a == 0, b == 0 has no equation left to solve ----
void testLinearNoEquation() {
  RtFloat t0 = 0.0, t1 = 0.0;
  int const numRoots = solveRayQuadratic(0.0, 0.0, 1.0, t0, t1);

  check(numRoots == 0, "linear: a == 0, b == 0 returns no roots");
}

// ---- check 3: a nonzero delegates to GMANQuadraticRoots unchanged ----
void testDelegatesToQuadraticRoots() {
  RtFloat expected0 = 0.0, expected1 = 0.0;
  int const expectedRoots = GMANQuadraticRoots(1.0, -3.0, 2.0, expected0, expected1);

  RtFloat t0 = 0.0, t1 = 0.0;
  int const numRoots = solveRayQuadratic(1.0, -3.0, 2.0, t0, t1);

  check(numRoots == expectedRoots, "delegate: root count matches GMANQuadraticRoots");
  check(t0 == expected0 && t1 == expected1, "delegate: roots match GMANQuadraticRoots");
}

} // namespace

int main() {
  testLinearRoot();
  testLinearNoEquation();
  testDelegatesToQuadraticRoots();

  return checkSummary("solveRayQuadratic solves the linear case and delegates otherwise");
}
