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
 * The shading-language spline family: the uniform cubic B-spline's values.
 */

#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "gmanslapi.h"

namespace {

constexpr float kTolerance = 1e-5f;

bool near(float a, float b) { return std::fabs(a - b) <= kTolerance; }

void testBsplineConstantCurve() {
  constexpr float k = 3.5f;
  std::vector<RtFloat> constant(6, k);
  for (const float value : {0.0f, 0.25f, 0.5f, 0.75f}) {
    const float result = GMANBsplineSpline<RtFloat>(value, 6, constant.data());
    check(near(result, k), "b-spline of a constant curve is the constant at value " + std::to_string(value));
  }
}

void testBsplineSegmentStart() {
  std::vector<RtFloat> points{0.0f, 1.0f, 0.0f, 0.0f};
  check(near(GMANBsplineSpline<RtFloat>(0.0f, 4, points.data()), 2.0f / 3.0f),
        "b-spline at value 0 is (p0 + 4 p1 + p2) / 6");
}

} // namespace

int main() {
  testBsplineConstantCurve();
  testBsplineSegmentStart();

  return checkSummary("the shading-language spline family evaluates the uniform cubic B-spline");
}
