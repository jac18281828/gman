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
 * GMANQuadraticRoots solves a*t^2 + b*t + c == 0 with the
 * numerically stable root form, reports the right root count at every
 * discriminant sign, and orders a 2-root answer t0 <= t1.
 */

#include <cmath>
#include <cstdio>

#include "check.h"
#include "gmanmath.h"

namespace {

bool relnear(RtFloat actual, double reference, double relTol) {
  return std::fabs(actual - reference) <= relTol * std::fabs(reference);
}

// ---- check 1: the stable form earns its place ----
// b^2 >> 4ac, where the textbook (-b +/- sqrt(disc)) / 2a cancels the small
// root to nothing. The reference values come from the same stable
// algebra evaluated in double, which does not suffer the cancellation a
// float evaluation of the naive form would.
void testStableFormEarnsItsPlace() {
  RtFloat const a = 1.0, b = 1.0e8, c = 1.0;

  double const disc = (double)b * b - 4.0 * a * c;
  double const sqrtDisc = std::sqrt(disc);
  double const q = -((double)b + sqrtDisc) / 2.0;
  double const referenceT0 = std::min(q / a, c / q);
  double const referenceT1 = std::max(q / a, c / q);

  RtFloat t0 = 0.0, t1 = 0.0;
  int const numRoots = GMANQuadraticRoots(a, b, c, t0, t1);

  std::printf("quadratic check 1: t0=%.9g (reference %.9g), t1=%.9g (reference %.9g)\n", (double)t0, referenceT0,
              (double)t1, referenceT1);

  check(numRoots == 2, "stable form: b^2 >> 4ac still returns two roots");
  check(relnear(t0, referenceT0, 1e-3), "stable form: the large root matches its reference within 1e-3");
  check(relnear(t1, referenceT1, 1e-3), "stable form: the small root matches its reference within 1e-3");
}

// ---- check 2: root counts ----
void testRootCounts() {
  RtFloat t0 = 0.0, t1 = 0.0;

  check(GMANQuadraticRoots(1.0, -3.0, 2.0, t0, t1) == 2, "root count: two distinct roots");
  check(GMANQuadraticRoots(1.0, -2.0, 1.0, t0, t1) == 1, "root count: one double root at disc == 0");
  check(GMANQuadraticRoots(1.0, 0.0, 1.0, t0, t1) == 0, "root count: none when disc < 0");
  check(GMANQuadraticRoots(0.0, 1.0, 1.0, t0, t1) == 0, "root count: 0 when a == 0");
}

// ---- check 3: ordering ----
void testOrdering() {
  RtFloat t0 = 0.0, t1 = 0.0;

  check(GMANQuadraticRoots(1.0, 0.0, -4.0, t0, t1) == 2 && t0 <= t1, "ordering: t0 <= t1 when the roots straddle zero");
  check(GMANQuadraticRoots(1.0, 3.0, 2.0, t0, t1) == 2 && t0 <= t1, "ordering: t0 <= t1 when both roots are negative");
}

} // namespace

int main() {
  testStableFormEarnsItsPlace();
  testRootCounts();
  testOrdering();

  return checkSummary("GMANQuadraticRoots solves, counts and orders correctly");
}
