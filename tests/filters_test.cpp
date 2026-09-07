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
 * Pins the five RISpec pixel-reconstruction filters (gmanfilters.cpp)
 * against their contracts, not their formulae: peak at the origin,
 * symmetry, zero outside support where a guard exists, monotone decay
 * where the shape allows it, and named-point values. None has a caller
 * yet (gmanfilters.cpp's header comment) -- this is the first evaluation
 * any of them has had.
 *
 * Revert check: reverting RiTriangleFilter's support guard or
 * RiGaussianFilter's argument scaling turns the corresponding assertions
 * below red (see the commit that lands the repair for the exact values).
 */

#include <cmath>

#include "check.h"
#include "ri.h"

namespace {

// 1e-4 clears float's ~7 significant digits on an exp/sin chain with
// room to spare, while still catching a wrong formula, not just rounding.
bool near(RtFloat a, RtFloat b, RtFloat tol = 1e-4) {
  return std::fabs(a - b) <= tol;
}

void testBoxFilter() {
  // RiBoxFilter is the RISpec reference verbatim: unclamped 1.0
  // everywhere, by settled decision (no support notion, no guard).
  check(near(RiBoxFilter(0.0, 0.0, 2.0, 2.0), 1.0), "box: 1.0 at the origin");
  check(near(RiBoxFilter(5.0, 5.0, 2.0, 2.0), 1.0),
        "box: 1.0 far outside its nominal width, unguarded by design");
}

void testTriangleFilter() {
  // Peak at the origin.
  check(near(RiTriangleFilter(0.0, 0.0, 2.0, 2.0), 1.0),
        "triangle: peak 1.0 at the origin");

  // Symmetric in x and y.
  check(near(RiTriangleFilter(0.5, 0.3, 2.0, 2.0),
             RiTriangleFilter(-0.5, 0.3, 2.0, 2.0)),
        "triangle: symmetric in x");
  check(near(RiTriangleFilter(0.5, 0.3, 2.0, 2.0),
             RiTriangleFilter(0.5, -0.3, 2.0, 2.0)),
        "triangle: symmetric in y");

  // Zero at the boundary, and zero -- not the unclamped negative or
  // sign-flipped-positive value -- everywhere outside it.
  check(near(RiTriangleFilter(1.0, 1.0, 2.0, 2.0), 0.0),
        "triangle: zero at the boundary");
  check(near(RiTriangleFilter(1.5, 1.5, 2.0, 2.0), 0.0),
        "triangle: guard zeroes an off-axis point outside support "
        "(unguarded this evaluates to +0.25)");
  check(near(RiTriangleFilter(1.5, 0.0, 2.0, 2.0), 0.0),
        "triangle: guard zeroes an on-axis point outside support "
        "(unguarded this evaluates to -0.5)");

  // Support scales with xwidth/ywidth, not a fixed default of 2.0 --
  // exercised at non-default, asymmetric widths so a guard that checks
  // the wrong axis's half-width, or one hardcoded to +-1.0, cannot pass
  // by coincidence at the width=2 cases above.
  check(near(RiTriangleFilter(2.0, 0.0, 4.0, 2.0), 0.0),
        "triangle: zero exactly at the x-boundary when xwidth != 2.0");
  check(near(RiTriangleFilter(0.5, 1.5, 4.0, 2.0), 0.0),
        "triangle: guard fires from y alone exceeding its narrower "
        "half-width while x stays in support "
        "(unguarded this evaluates to -0.375)");
  check(near(RiTriangleFilter(1.5, 0.0, 8.0, 2.0), 0.625),
        "triangle: in-support point survives a wide xwidth that a "
        "hardcoded +-1.0 guard, or one checking ywidth instead of "
        "xwidth, would wrongly zero");

  // Monotone decay from the centre out to the boundary.
  check(RiTriangleFilter(0.2, 0.0, 2.0, 2.0) >
            RiTriangleFilter(0.6, 0.0, 2.0, 2.0),
        "triangle: monotone decay along x");
  check(RiTriangleFilter(0.6, 0.0, 2.0, 2.0) >
            RiTriangleFilter(0.9, 0.0, 2.0, 2.0),
        "triangle: monotone decay continues toward the boundary");
}

void testGaussianFilter() {
  // Peak at the origin.
  check(near(RiGaussianFilter(0.0, 0.0, 2.0, 2.0), 1.0),
        "gaussian: peak 1.0 at the origin");

  // Symmetric in x and y.
  check(near(RiGaussianFilter(0.7, 0.4, 2.0, 2.0),
             RiGaussianFilter(-0.7, 0.4, 2.0, 2.0)),
        "gaussian: symmetric in x");
  check(near(RiGaussianFilter(0.7, 0.4, 2.0, 2.0),
             RiGaussianFilter(0.7, -0.4, 2.0, 2.0)),
        "gaussian: symmetric in y");

  // The scaled exponent, exp(-8*x^2/xwidth^2) at width 2: at x=1,
  // width=2, exp(-8*0.25) = exp(-2) = 0.1353 (the unscaled form gives
  // exp(-0.5) = 0.6065).
  check(near(RiGaussianFilter(1.0, 0.0, 2.0, 2.0), 0.1353),
        "gaussian: edge sample matches the RISpec-scaled reference "
        "(unrescaled this evaluates to 0.6065)");

  // Self-similar under a scaled width: exp(-2*((2*x/xwidth))^2) has the
  // same value at (2x, 2*xwidth) as at (x, xwidth), checked away from
  // the default width=2 so a rescale that only matches there cannot
  // pass by coincidence.
  check(near(RiGaussianFilter(2.0, 0.0, 4.0, 4.0), 0.1353),
        "gaussian: doubling x and xwidth together reproduces the "
        "width-2 edge value");

  // Monotone decay from the centre out.
  check(RiGaussianFilter(0.2, 0.0, 2.0, 2.0) >
            RiGaussianFilter(0.6, 0.0, 2.0, 2.0),
        "gaussian: monotone decay along x");
  check(RiGaussianFilter(0.6, 0.0, 2.0, 2.0) >
            RiGaussianFilter(1.0, 0.0, 2.0, 2.0),
        "gaussian: monotone decay continues to the boundary");
}

void testCatmullRomFilter() {
  // Pinned as found: separable, width-independent, peaks at the origin.
  // A real design divergence from the RISpec's radial reference, and out
  // of scope for this task -- see gmanfilters.cpp's header comment.
  check(near(RiCatmullRomFilter(0.0, 0.0, 2.0, 2.0), 1.0),
        "catmull-rom: peak 1.0 at the origin");

  check(near(RiCatmullRomFilter(0.5, 0.3, 2.0, 2.0),
             RiCatmullRomFilter(-0.5, 0.3, 2.0, 2.0)),
        "catmull-rom: symmetric in x");
  check(near(RiCatmullRomFilter(0.5, 0.3, 2.0, 2.0),
             RiCatmullRomFilter(0.5, -0.3, 2.0, 2.0)),
        "catmull-rom: symmetric in y");

  // Width-independent: xwidth/ywidth are ignored, unlike box, triangle
  // and gaussian.
  check(near(RiCatmullRomFilter(0.5, 0.0, 2.0, 2.0),
             RiCatmullRomFilter(0.5, 0.0, 8.0, 8.0)),
        "catmull-rom: ignores xwidth/ywidth, as found");

  // Zero outside |x|>=2 and |y|>=2.
  check(near(RiCatmullRomFilter(2.0, 0.0, 2.0, 2.0), 0.0),
        "catmull-rom: zero at x=2");
  check(near(RiCatmullRomFilter(3.0, 0.0, 2.0, 2.0), 0.0),
        "catmull-rom: zero beyond x=2");

  // Genuine negative side lobe -- not monotone, pinned as found rather
  // than asserted monotone (which would be false).
  check(RiCatmullRomFilter(1.5, 0.0, 2.0, 2.0) < 0.0,
        "catmull-rom: negative side lobe at x=1.5");
  check(near(RiCatmullRomFilter(1.5, 0.0, 2.0, 2.0), -0.0625),
        "catmull-rom: side lobe value at x=1.5 is -0.0625");
  check(RiCatmullRomFilter(2.0, 0.0, 2.0, 2.0) >
            RiCatmullRomFilter(1.5, 0.0, 2.0, 2.0),
        "catmull-rom: rises again from the side lobe toward x=2");
}

void testSincFilter() {
  // Pinned as found: infinite support by construction, no guard.
  check(near(RiSincFilter(0.0, 0.0, 2.0, 2.0), 1.0),
        "sinc: peak 1.0 at the origin");

  check(
      near(RiSincFilter(0.5, 0.3, 2.0, 2.0), RiSincFilter(-0.5, 0.3, 2.0, 2.0)),
      "sinc: symmetric in x");
  check(
      near(RiSincFilter(0.5, 0.3, 2.0, 2.0), RiSincFilter(0.5, -0.3, 2.0, 2.0)),
      "sinc: symmetric in y");

  // Nonzero far outside any nominal width -- unwindowed, no guard, by
  // design. A windowed sinc would clamp this to 0; this one does not.
  check(near(RiSincFilter(10.5, 0.0, 2.0, 2.0), 0.0303),
        "sinc: nonzero far outside width 2, unguarded by design");

  // Genuine negative side lobe between x=1 and x=2, not monotone.
  check(RiSincFilter(1.5, 0.0, 2.0, 2.0) < 0.0,
        "sinc: negative side lobe at x=1.5");

  // Zero at every nonzero integer.
  check(near(RiSincFilter(1.0, 0.0, 2.0, 2.0), 0.0), "sinc: zero at x=1");
  check(near(RiSincFilter(2.0, 0.0, 2.0, 2.0), 0.0), "sinc: zero at x=2");
}

}  // namespace

int main() {
  testBoxFilter();
  testTriangleFilter();
  testGaussianFilter();
  testCatmullRomFilter();
  testSincFilter();

  return checkSummary("filters holds");
}
