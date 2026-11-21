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
 * GMANMax(a, b) answers a when a > b and b otherwise; GMANMin(a, b) answers
 * a when a < b and b otherwise. A NaN comparison is always false, so a NaN
 * first operand yields the second, and a NaN second operand yields NaN.
 * std::max and std::min answer the other way round for a NaN first operand,
 * and the shader clamps depend on gman's order: GMANMin(GMANMax(NaN, 0), 1)
 * maps a NaN channel to +0, where the std templates would pass NaN through.
 * That is why gman keeps its own templates over std::max and std::min.
 *
 * The arithmetic helpers GMANRadians, GMANDegrees, GMANSign, GMANMin,
 * GMANMax, GMANClamp and GMANMix are constexpr. The static_asserts below
 * pin that: removing constexpr from one fails the build.
 */

#include <cmath>
#include <limits>
#include <string>

#include "check.h"
#include "gmanmath.h"

namespace {

template <class T> void checkMinMax(std::string const& type) {
  T const nan = std::numeric_limits<T>::quiet_NaN();
  T const low = static_cast<T>(1.5);
  T const high = static_cast<T>(2.5);

  check(GMANMax(nan, high) == high, type + ": GMANMax(NaN, t) == t");
  check(std::isnan(GMANMax(high, nan)), type + ": GMANMax(t, NaN) is NaN");
  check(GMANMin(nan, high) == high, type + ": GMANMin(NaN, t) == t");
  check(std::isnan(GMANMin(high, nan)), type + ": GMANMin(t, NaN) is NaN");

  check(GMANMax(low, high) == high && GMANMax(high, low) == high,
        type + ": GMANMax returns the larger operand either way round");
  check(GMANMin(low, high) == low && GMANMin(high, low) == low,
        type + ": GMANMin returns the smaller operand either way round");
  check(GMANMax(high, high) == high && GMANMin(high, high) == high, type + ": equal operands return that value");

  T const clamped = GMANMin(GMANMax(nan, static_cast<T>(0)), static_cast<T>(1));
  check(clamped == static_cast<T>(0) && !std::signbit(clamped), type + ": GMANMin(GMANMax(NaN, 0), 1) is +0");
}

} // namespace

static_assert(GMANRadians(0.0f) == 0.0f);
static_assert(GMANDegrees(0.0f) == 0.0f);
static_assert(GMANSign(-2.0f) == -1.0f);
static_assert(GMANMin(1.0f, 2.0f) == 1.0f);
static_assert(GMANMax(1.0f, 2.0f) == 2.0f);
static_assert(GMANClamp(3.0f, 0.0f, 1.0f) == 1.0f);
static_assert(GMANMix(2.0f, 4.0f, 0.5f) == 3.0f);

int main() {
  checkMinMax<float>("float");
  checkMinMax<double>("double");
  return checkSummary("GMANMax and GMANMin keep the NaN contract the shader clamps rely on");
}
