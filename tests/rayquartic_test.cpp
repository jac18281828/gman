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
 * gman::solveQuartic against quartics built from known
 * dyadic roots (integers or halves), so the double coefficients have
 * exactly those roots and the comparison is exact arithmetic, not a fit.
 * Each case expands the product of (t - root) factors by hand in the
 * comment above it.
 */

#include <algorithm>
#include <cmath>
#include <string>

#include "check.h"
#include "gmanrayquartic.h"

namespace {

// Simple roots agree to 1e-9 relative to the largest root's magnitude; a
// double root (tested via testDoubleRootBesideSimpleRoots's own tolerance)
// to 1e-6, the closed form itself conditioned more poorly there against a
// vanishing derivative.
void checkRoots(char const* name, double a, double b, double c, double d, double e, double const* expected,
                int expectedCount, double relTol) {
  double roots[4];
  int const count = gman::solveQuartic(a, b, c, d, e, roots);
  check(count == expectedCount, std::string(name) + ": root count == " + std::to_string(expectedCount) + " (got " +
                                    std::to_string(count) + ")");
  if (count != expectedCount)
    return;

  double scale = 0.0;
  for (int i = 0; i < expectedCount; ++i)
    scale = std::max(scale, std::fabs(expected[i]));
  if (scale == 0.0)
    scale = 1.0;

  for (int i = 0; i < expectedCount; ++i) {
    double const err = std::fabs(roots[i] - expected[i]) / scale;
    check(err <= relTol, std::string(name) + ": root " + std::to_string(i) + " == " + std::to_string(expected[i]) +
                             " (got " + std::to_string(roots[i]) + ", relative error " + std::to_string(err) + ")");
  }
}

// (t+2)(t+0.5)(t-0.5)(t-3) == t^4 - t^3 - 6.25t^2 + 0.25t + 1.5
void testMixedSignRoots() {
  double const expected[4] = {-2.0, -0.5, 0.5, 3.0};
  checkRoots("mixed sign", 1.0, -1.0, -6.25, 0.25, 1.5, expected, 4, 1e-9);
}

// (t-1)(t+1)(t^2+1) == t^4 - 1: two real roots, a complex pair.
void testTwoRealTwoComplex() {
  double const expected[2] = {-1.0, 1.0};
  checkRoots("two real, complex pair", 1.0, 0.0, 0.0, 0.0, -1.0, expected, 2, 1e-9);
}

// (t^2+1)(t^2+4) == t^4 + 5t^2 + 4: no real roots at all.
void testNoRealRoots() { checkRoots("no real roots", 1.0, 0.0, 5.0, 0.0, 4.0, nullptr, 0, 1e-9); }

// (t^2-1)(t^2-4) == t^4 - 5t^2 + 4, b == d == 0: the biquadratic special
// case, an axial ray's own shape.
void testBiquadratic() {
  double const expected[4] = {-2.0, -1.0, 1.0, 2.0};
  checkRoots("biquadratic", 1.0, 0.0, -5.0, 0.0, 4.0, expected, 4, 1e-9);
}

// (t-1)^2(t+1)(t-3) == t^4 - 4t^3 + 2t^2 + 4t - 3: a grazing ray's double
// root beside two simple ones.
void testDoubleRootBesideSimpleRoots() {
  double const expected[4] = {-1.0, 1.0, 1.0, 3.0};
  checkRoots("double root", 1.0, -4.0, 2.0, 4.0, -3.0, expected, 4, 1e-6);
}

// (t^2-1)(t^2-0.25) == t^4 - 1.25t^2 + 0.25: four roots clustered within 2
// of each other near zero -- a shifted far ray's own shape.
void testClusteredRoots() {
  double const expected[4] = {-1.0, -0.5, 0.5, 1.0};
  checkRoots("clustered", 1.0, 0.0, -1.25, 0.0, 0.25, expected, 4, 1e-9);
}

// (t+1.5)(t-2)(t^2+2t+5): a real pair beside a complex pair, taking the
// Ferrari path (b, d != 0) rather than the biquadratic special case.
void testFerrariComplexPair() {
  double const expected[2] = {-1.5, 2.0};
  checkRoots("Ferrari path, complex pair", 1.0, 1.5, 1.0, -8.5, -15.0, expected, 2, 1e-9);
}

// (t^2+t+1)(t^2+3t+10): no real roots at all, taking the Ferrari path.
void testFerrariNoRealRoots() {
  checkRoots("Ferrari path, no real roots", 1.0, 4.0, 14.0, 13.0, 10.0, nullptr, 0, 1e-9);
}

// (t+0.5)(t-0.5-2^-21)(t^2-2^-22*t+4): q is small but nonzero and the
// resolvent cubic's own root sits near zero, where n - p/3 (the depressed
// cubic's root, shifted back) cancels catastrophically. Regression for the
// speckle a horizontal ray (object-space dz ~= 0) showed on the torus.
void testNearZeroResolventRoot() {
  double const expected[2] = {-0.5, 0.5000004768371582};
  checkRoots("near-zero resolvent root", 1.0, -7.152557373046875e-07, 3.7499997615815346, -1.8477439311936905e-06,
             -1.0000009536743164, expected, 2, 1e-6);
}

// (t+0.5)(t-0.5-2^-23)(t^2+2^-22*t+4): a second near-zero resolvent root, one
// bit narrower than testNearZeroResolventRoot's, regressing the same
// cancellation at a different magnitude.
void testNearZeroResolventRootNarrower() {
  double const expected[2] = {-0.5, 0.50000011920928955};
  checkRoots("near-zero resolvent root, narrower", 1.0, 1.1920928955078125e-07, 3.7499999403953268,
             -5.3644181718937034e-07, -1.0000002384185791, expected, 2, 1e-6);
}

} // namespace

int main() {
  testMixedSignRoots();
  testTwoRealTwoComplex();
  testNoRealRoots();
  testBiquadratic();
  testDoubleRootBesideSimpleRoots();
  testClusteredRoots();
  testFerrariComplexPair();
  testFerrariNoRealRoots();
  testNearZeroResolventRoot();
  testNearZeroResolventRootNarrower();

  return checkSummary("gman::solveQuartic solves a ray's quartic at every discriminant shape");
}
