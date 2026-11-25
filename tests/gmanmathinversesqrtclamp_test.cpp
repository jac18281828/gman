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
 * The shading language defines inversesqrt(x) as 1 / sqrt(x), and IEEE 754
 * gives +infinity at +0, -infinity at -0 and NaN below 0 or for NaN.
 * GMANInversesqrt answers RI_INFINITY for every x whose square root is at most
 * RI_EPSILON, and for NaN. WILL_FAIL until GMANInversesqrt follows IEEE there.
 */

#include <cmath>
#include <format>
#include <limits>

#include "check.h"
#include "gmanmath.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInf = std::numeric_limits<RtFloat>::infinity();
constexpr double kTolerance = 1e-6;

void expectInfinity(RtFloat x, RtFloat expected) {
  const RtFloat actual = GMANInversesqrt(x);
  check(actual == expected, std::format("GMANInversesqrt({}): expected {}, got {}", x, expected, actual));
}

void expectNaN(RtFloat x) {
  const RtFloat actual = GMANInversesqrt(x);
  check(std::isnan(actual), std::format("GMANInversesqrt({}): expected NaN, got {}", x, actual));
}

} // namespace

int main() {
  const RtFloat x = RI_EPSILON * RI_EPSILON / 4;
  const RtFloat belowClamp = GMANInversesqrt(x);
  constexpr double expected = 2e10;
  check(std::fabs(belowClamp - expected) <= kTolerance * expected,
        std::format("GMANInversesqrt({}): expected {} within tolerance, got {}", x, expected, belowClamp));
  expectInfinity(0.0f, kInf);
  expectInfinity(-0.0f, -kInf);
  expectNaN(-1.0f);
  expectNaN(-kInf);
  expectNaN(kNaN);
  return checkSummary("GMANInversesqrt gives its IEEE result below RI_EPSILON, at zero and for NaN");
}
