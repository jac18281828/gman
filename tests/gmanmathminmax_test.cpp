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
 * GMANMin, GMANMax and GMANClamp drop a NaN operand for float and double, as
 * C's fmin and fmax do: the number answers whichever side the NaN is on, a NaN
 * value clamps to the lower bound, and only two NaN operands answer NaN. The
 * contract holds in constant expressions and at run time. Every other type
 * answers as std::min, std::max and std::clamp do, including 64-bit integers
 * beyond a double's 53-bit mantissa.
 *
 * The arithmetic helpers GMANRadians, GMANDegrees, GMANSign, GMANMin,
 * GMANMax, GMANClamp and GMANMix are constexpr. The static_asserts below
 * pin that: removing constexpr from one fails the build.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "gmanmath.h"

namespace {

template <class T> constexpr bool dropsNaN() {
  constexpr T nan = std::numeric_limits<T>::quiet_NaN();
  constexpr T low = static_cast<T>(1.5);
  constexpr T high = static_cast<T>(2.5);
  return GMANMax(nan, high) == high && GMANMax(high, nan) == high && GMANMin(nan, high) == high &&
         GMANMin(high, nan) == high && GMANClamp(nan, low, high) == low && GMANClamp(high, nan, low) == low &&
         GMANClamp(low, low, nan) == low && GMANClamp(nan, nan, high) == high;
}

template <class T> constexpr bool ordersNumbers() {
  constexpr T low = static_cast<T>(1.5);
  constexpr T high = static_cast<T>(2.5);
  return GMANMax(low, high) == high && GMANMax(high, low) == high && GMANMin(low, high) == low &&
         GMANMin(high, low) == low && GMANMax(high, high) == high && GMANMin(high, high) == high &&
         GMANClamp(static_cast<T>(0), low, high) == low && GMANClamp(static_cast<T>(3), low, high) == high &&
         GMANClamp(static_cast<T>(2), low, high) == static_cast<T>(2);
}

static_assert(dropsNaN<float>());
static_assert(dropsNaN<double>());
static_assert(ordersNumbers<float>());
static_assert(ordersNumbers<double>());

template <class T> void checkFloating(std::string const& type) {
  T const nan = std::numeric_limits<T>::quiet_NaN();
  T const low = static_cast<T>(1.5);
  T const high = static_cast<T>(2.5);

  check(GMANMax(nan, high) == high, type + ": GMANMax(NaN, t) == t");
  check(GMANMax(high, nan) == high, type + ": GMANMax(t, NaN) == t");
  check(GMANMin(nan, high) == high, type + ": GMANMin(NaN, t) == t");
  check(GMANMin(high, nan) == high, type + ": GMANMin(t, NaN) == t");
  check(std::isnan(GMANMax(nan, nan)), type + ": GMANMax(NaN, NaN) is NaN");
  check(std::isnan(GMANMin(nan, nan)), type + ": GMANMin(NaN, NaN) is NaN");

  check(GMANClamp(nan, low, high) == low, type + ": GMANClamp(NaN, lo, hi) == lo");
  check(GMANClamp(high, nan, low) == low, type + ": GMANClamp(t, NaN, hi) takes hi");
  check(GMANClamp(low, low, nan) == low, type + ": GMANClamp(t, lo, NaN) ignores the NaN bound");
  check(GMANClamp(static_cast<T>(0), static_cast<T>(0), static_cast<T>(1)) == static_cast<T>(0) &&
            GMANClamp(nan, static_cast<T>(0), static_cast<T>(1)) == static_cast<T>(0),
        type + ": a NaN channel clamped to [0, 1] is 0");

  check(GMANMax(low, high) == high && GMANMax(high, low) == high,
        type + ": GMANMax returns the larger operand either way round");
  check(GMANMin(low, high) == low && GMANMin(high, low) == low,
        type + ": GMANMin returns the smaller operand either way round");
  check(GMANMax(high, high) == high && GMANMin(high, high) == high, type + ": equal operands return that value");
  check(GMANClamp(static_cast<T>(0), low, high) == low && GMANClamp(static_cast<T>(3), low, high) == high &&
            GMANClamp(static_cast<T>(2), low, high) == static_cast<T>(2),
        type + ": GMANClamp bounds an ordinary value");
  check(dropsNaN<T>() && ordersNumbers<T>(), type + ": the run-time path agrees with the constant-expression path");
}

template <class T> void checkStandard(std::string const& type, std::vector<T> const& values) {
  bool minMax = true;
  for (T const a : values) {
    for (T const b : values) {
      minMax = minMax && GMANMin(a, b) == std::min(a, b) && GMANMax(a, b) == std::max(a, b);
    }
  }
  check(minMax, type + ": GMANMin and GMANMax equal std::min and std::max");

  bool clamp = true;
  for (T const lo : values) {
    for (T const hi : values) {
      if (hi < lo) {
        continue;
      }
      for (T const v : values) {
        clamp = clamp && GMANClamp(v, lo, hi) == std::clamp(v, lo, hi);
      }
    }
  }
  check(clamp, type + ": GMANClamp equals std::clamp");
}

void checkWideIntegers() {
  constexpr std::int64_t base = std::int64_t{1} << 53;
  check(GMANMax(base + 2, base + 3) == base + 3, "int64_t: GMANMax(2^53 + 2, 2^53 + 3) is exact");
  check(GMANMax(base + 3, base + 2) == base + 3, "int64_t: GMANMax(2^53 + 3, 2^53 + 2) is exact");
  check(GMANMin(base + 1, base + 2) == base + 1, "int64_t: GMANMin(2^53 + 1, 2^53 + 2) is exact");
  check(GMANMin(base + 2, base + 1) == base + 1, "int64_t: GMANMin(2^53 + 2, 2^53 + 1) is exact");
  check(GMANClamp(base + 3, base + 1, base + 2) == base + 2, "int64_t: GMANClamp is exact beyond 2^53");
}

} // namespace

static_assert(GMANRadians(0.0f) == 0.0f);
static_assert(GMANDegrees(0.0f) == 0.0f);
static_assert(GMANSign(-2.0f) == -1.0f);
static_assert(GMANMin(1.0f, 2.0f) == 1.0f);
static_assert(GMANMax(1.0f, 2.0f) == 2.0f);
static_assert(GMANClamp(3.0f, 0.0f, 1.0f) == 1.0f);
static_assert(GMANMix(2.0f, 4.0f, 0.5f) == 3.0f);
static_assert(GMANMin(1, 2) == 1);
static_assert(GMANMax(1, 2) == 2);
static_assert(GMANClamp(3, 0, 1) == 1);

int main() {
  checkFloating<float>("float");
  checkFloating<double>("double");
  checkStandard<int>("int", {-7, -1, 0, 1, 7});
  checkStandard<unsigned>("unsigned", {0u, 1u, 7u, 4000000000u});
  checkStandard<std::int64_t>("int64_t", {-(std::int64_t{1} << 53) - 1, -3, 0, 3, (std::int64_t{1} << 53) + 1,
                                          (std::int64_t{1} << 53) + 2, (std::int64_t{1} << 53) + 3});
  checkWideIntegers();
  return checkSummary("GMANMin, GMANMax and GMANClamp drop a NaN operand and follow the standard library elsewhere");
}
