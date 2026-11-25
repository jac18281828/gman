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
 * The RenderMan Shading Language functions of gmanmath.h, checked at run time
 * against their definitions: radians, degrees, atan, inversesqrt, log, mod,
 * sign, mix and round. Floating-point edges follow IEEE 754 and C's Annex F,
 * and a NaN operand gives NaN. DEGTORAD is checked through GMANRadians and
 * GMANDegrees. GMANMin, GMANMax and GMANClamp are in gmanmathminmax_test.cpp.
 *
 * Inputs whose result the language leaves open, such as a rounding tie or the
 * sign of a zero, run and print their result without asserting it. Defects
 * are pinned in the gmanmath*_test.cpp executables, each a failing test.
 */

#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <limits>
#include <numbers>
#include <string>

#include "check.h"
#include "gmanmath.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInf = std::numeric_limits<RtFloat>::infinity();
constexpr RtFloat kMax = std::numeric_limits<RtFloat>::max();
constexpr RtFloat kDenormMin = std::numeric_limits<RtFloat>::denorm_min();
constexpr double kTolerance = 1e-6;

template <class... Args> std::string describe(std::string const& name, Args... args) {
  std::string text = std::format("{}(", name);
  char const* separator = "";
  ((text += std::format("{}{}", separator, args), separator = ", "), ...);
  return text + ")";
}

void expectNear(std::string const& call, double actual, double expected) {
  const double diff = std::fabs(actual - expected);
  const bool ok = diff <= kTolerance * std::fabs(expected) || diff <= kTolerance;
  check(ok, std::format("{}: expected {} within tolerance, got {}", call, expected, actual));
}

void expectRelative(std::string const& call, double actual, double expected) {
  const bool ok = std::fabs(actual - expected) <= kTolerance * std::fabs(expected);
  check(ok, std::format("{}: expected {} within relative tolerance, got {}", call, expected, actual));
}

void expectExactly(std::string const& call, double actual, double expected) {
  const bool ok = actual == expected && std::signbit(actual) == std::signbit(expected);
  check(ok, std::format("{}: expected {}, got {}", call, expected, actual));
}

void expectZeroValued(std::string const& call, double actual) {
  check(actual == 0.0, std::format("{}: expected a zero, got {}", call, actual));
}

void expectNaN(std::string const& call, double actual) {
  check(std::isnan(actual), std::format("{}: expected NaN, got {}", call, actual));
}

template <class... Args> void observe(char const* name, RtFloat result, Args... args) {
  std::printf("unspecified: %s -> %s\n", describe(name, args...).c_str(), std::format("{}", result).c_str());
}

void checkRadiansAndDegrees() {
  struct Angle {
    RtFloat degrees;
    RtFloat radians;
  };
  const std::array angles = {Angle{45, std::numbers::pi_v<RtFloat> / 4},   Angle{90, std::numbers::pi_v<RtFloat> / 2},
                             Angle{180, std::numbers::pi_v<RtFloat>},      Angle{360, 2 * std::numbers::pi_v<RtFloat>},
                             Angle{-90, -std::numbers::pi_v<RtFloat> / 2}, Angle{-180, -std::numbers::pi_v<RtFloat>},
                             Angle{1, std::numbers::pi_v<RtFloat> / 180}};
  for (auto const& [degrees, radians] : angles) {
    expectNear(describe("GMANRadians", degrees), GMANRadians(degrees), radians);
    expectNear(describe("GMANDegrees", radians), GMANDegrees(radians), degrees);
  }

  expectExactly(describe("GMANRadians", 0.0f), GMANRadians(0.0f), 0.0);
  expectExactly(describe("GMANRadians", -0.0f), GMANRadians(-0.0f), -0.0);
  expectExactly(describe("GMANDegrees", 0.0f), GMANDegrees(0.0f), 0.0);
  expectExactly(describe("GMANDegrees", -0.0f), GMANDegrees(-0.0f), -0.0);
  expectExactly(describe("GMANRadians", kInf), GMANRadians(kInf), kInf);
  expectExactly(describe("GMANRadians", -kInf), GMANRadians(-kInf), -kInf);
  expectExactly(describe("GMANDegrees", kInf), GMANDegrees(kInf), kInf);
  expectExactly(describe("GMANDegrees", -kInf), GMANDegrees(-kInf), -kInf);
  expectNaN(describe("GMANRadians", kNaN), GMANRadians(kNaN));
  expectNaN(describe("GMANDegrees", kNaN), GMANDegrees(kNaN));
}

void checkAtan() {
  constexpr RtFloat angle345 = 0.6435011f;
  struct Quadrant {
    RtFloat y;
    RtFloat x;
    RtFloat expected;
  };
  const std::array quadrants = {Quadrant{1, 1, std::numbers::pi_v<RtFloat> / 4},
                                Quadrant{1, -1, 3 * std::numbers::pi_v<RtFloat> / 4},
                                Quadrant{-1, -1, -3 * std::numbers::pi_v<RtFloat> / 4},
                                Quadrant{-1, 1, -std::numbers::pi_v<RtFloat> / 4},
                                Quadrant{1, 0, std::numbers::pi_v<RtFloat> / 2},
                                Quadrant{-1, 0, -std::numbers::pi_v<RtFloat> / 2},
                                Quadrant{0, -1, std::numbers::pi_v<RtFloat>},
                                Quadrant{-0.0f, -1, -std::numbers::pi_v<RtFloat>},
                                Quadrant{3, 4, angle345},
                                Quadrant{3, -4, std::numbers::pi_v<RtFloat> - angle345},
                                Quadrant{-3, -4, angle345 - std::numbers::pi_v<RtFloat>},
                                Quadrant{-3, 4, -angle345},
                                Quadrant{kInf, 1, std::numbers::pi_v<RtFloat> / 2},
                                Quadrant{-kInf, 1, -std::numbers::pi_v<RtFloat> / 2},
                                Quadrant{1, -kInf, std::numbers::pi_v<RtFloat>},
                                Quadrant{-1, -kInf, -std::numbers::pi_v<RtFloat>},
                                Quadrant{kInf, kInf, std::numbers::pi_v<RtFloat> / 4},
                                Quadrant{kInf, -kInf, 3 * std::numbers::pi_v<RtFloat> / 4},
                                Quadrant{0, -0.0f, std::numbers::pi_v<RtFloat>},
                                Quadrant{-0.0f, -0.0f, -std::numbers::pi_v<RtFloat>}};
  for (auto const& [y, x, expected] : quadrants) {
    expectNear(describe("GMANAtan", y, x), GMANAtan(y, x), expected);
  }

  expectExactly(describe("GMANAtan", 0.0f, 1.0f), GMANAtan(0.0f, 1.0f), 0.0);
  expectExactly(describe("GMANAtan", -0.0f, 1.0f), GMANAtan(-0.0f, 1.0f), -0.0);
  expectExactly(describe("GMANAtan", 0.0f, 0.0f), GMANAtan(0.0f, 0.0f), 0.0);
  expectExactly(describe("GMANAtan", -0.0f, 0.0f), GMANAtan(-0.0f, 0.0f), -0.0);
  expectExactly(describe("GMANAtan", 1.0f, kInf), GMANAtan(1.0f, kInf), 0.0);
  expectExactly(describe("GMANAtan", -1.0f, kInf), GMANAtan(-1.0f, kInf), -0.0);
  expectNaN(describe("GMANAtan", kNaN, 1.0f), GMANAtan(kNaN, 1.0f));
  expectNaN(describe("GMANAtan", 1.0f, kNaN), GMANAtan(1.0f, kNaN));
  expectNaN(describe("GMANAtan", kNaN, kNaN), GMANAtan(kNaN, kNaN));
}

void checkInversesqrt() {
  struct Exact {
    RtFloat x;
    RtFloat expected;
  };
  const std::array exact = {Exact{4, 0.5f}, Exact{16, 0.25f}, Exact{1, 1}, Exact{0.25f, 2}, Exact{0x1p-40f, 0x1p20f}};
  for (auto const& [x, expected] : exact) {
    expectExactly(describe("GMANInversesqrt", x), GMANInversesqrt(x), expected);
  }

  struct Inexact {
    RtFloat x;
    double expected;
  };
  const std::array inexact = {Inexact{2, std::numbers::sqrt2_v<RtFloat> / 2},
                              Inexact{4e-20f, 5e9},
                              Inexact{1.0000001e-20f, 1e10},
                              Inexact{1e-19f, 3.16227766e9},
                              Inexact{RI_INFINITY, 1e-19},
                              Inexact{kMax, 0x1p-64}};
  for (auto const& [x, expected] : inexact) {
    expectRelative(describe("GMANInversesqrt", x), GMANInversesqrt(x), expected);
  }

  expectExactly(describe("GMANInversesqrt", kInf), GMANInversesqrt(kInf), 0.0);
}

void checkLogFn() {
  struct Finite {
    RtFloat x;
    RtFloat base;
    double expected;
  };
  const std::array finite = {Finite{8, 2, 3},     Finite{100, 10, 2}, Finite{0.5f, 2, -1},   Finite{2, 0.5f, -1},
                             Finite{1000, 10, 3}, Finite{9, 3, 2},    Finite{0.125f, 2, -3}, Finite{1024, 2, 10}};
  for (auto const& [x, base, expected] : finite) {
    expectNear(describe("GMANLogFn", x, base), GMANLogFn(x, base), expected);
  }

  expectExactly(describe("GMANLogFn", 1.0f, 2.0f), GMANLogFn(1.0f, 2.0f), 0.0);
  expectExactly(describe("GMANLogFn", 1.0f, 10.0f), GMANLogFn(1.0f, 10.0f), 0.0);

  expectExactly(describe("GMANLogFn", 0.0f, 2.0f), GMANLogFn(0.0f, 2.0f), -kInf);
  expectExactly(describe("GMANLogFn", -0.0f, 2.0f), GMANLogFn(-0.0f, 2.0f), -kInf);
  expectExactly(describe("GMANLogFn", 0.0f, 0.5f), GMANLogFn(0.0f, 0.5f), kInf);
  expectExactly(describe("GMANLogFn", -0.0f, 0.5f), GMANLogFn(-0.0f, 0.5f), kInf);
  expectExactly(describe("GMANLogFn", kInf, 2.0f), GMANLogFn(kInf, 2.0f), kInf);
  expectExactly(describe("GMANLogFn", 2.0f, kInf), GMANLogFn(2.0f, kInf), 0.0);
  expectExactly(describe("GMANLogFn", 0.5f, kInf), GMANLogFn(0.5f, kInf), -0.0);

  expectExactly(describe("GMANLogFn", 2.0f, 1.0f), GMANLogFn(2.0f, 1.0f), kInf);
  expectExactly(describe("GMANLogFn", 0.5f, 1.0f), GMANLogFn(0.5f, 1.0f), -kInf);
  expectExactly(describe("GMANLogFn", 0.0f, 1.0f), GMANLogFn(0.0f, 1.0f), -kInf);
  expectExactly(describe("GMANLogFn", -0.0f, 1.0f), GMANLogFn(-0.0f, 1.0f), -kInf);
  expectNaN(describe("GMANLogFn", 1.0f, 1.0f), GMANLogFn(1.0f, 1.0f));

  for (RtFloat const zero : {0.0f, -0.0f}) {
    expectExactly(describe("GMANLogFn", 2.0f, zero), GMANLogFn(2.0f, zero), -0.0);
    expectExactly(describe("GMANLogFn", 1.0f, zero), GMANLogFn(1.0f, zero), -0.0);
    expectExactly(describe("GMANLogFn", 0.5f, zero), GMANLogFn(0.5f, zero), 0.0);
    expectNaN(describe("GMANLogFn", 0.0f, zero), GMANLogFn(0.0f, zero));
  }

  expectNaN(describe("GMANLogFn", -1.0f, 2.0f), GMANLogFn(-1.0f, 2.0f));
  expectNaN(describe("GMANLogFn", 2.0f, -2.0f), GMANLogFn(2.0f, -2.0f));
  expectNaN(describe("GMANLogFn", -1.0f, -2.0f), GMANLogFn(-1.0f, -2.0f));
  expectNaN(describe("GMANLogFn", 1.0f, -2.0f), GMANLogFn(1.0f, -2.0f));
  expectNaN(describe("GMANLogFn", kNaN, 2.0f), GMANLogFn(kNaN, 2.0f));
  expectNaN(describe("GMANLogFn", 2.0f, kNaN), GMANLogFn(2.0f, kNaN));
  expectNaN(describe("GMANLogFn", kNaN, kNaN), GMANLogFn(kNaN, kNaN));
}

void checkMod() {
  struct Remainder {
    RtFloat a;
    RtFloat b;
    RtFloat expected;
  };
  const std::array remainders = {Remainder{5, 3, 2},          Remainder{7.5f, 2, 1.5f},  Remainder{0.5f, 1, 0.5f},
                                 Remainder{2, 5, 2},          Remainder{-1, 3, 2},       Remainder{-5, 3, 1},
                                 Remainder{-7, 3, 2},         Remainder{-0.5f, 2, 1.5f}, Remainder{-7.5f, 2, 0.5f},
                                 Remainder{-0.25f, 1, 0.75f}, Remainder{-2, 5, 3}};
  for (auto const& [a, b, expected] : remainders) {
    expectExactly(describe("GMANMod", a, b), GMANMod(a, b), expected);
  }

  expectZeroValued(describe("GMANMod", 6.0f, 3.0f), GMANMod(6.0f, 3.0f));
  expectZeroValued(describe("GMANMod", 0.0f, 3.0f), GMANMod(0.0f, 3.0f));
  expectZeroValued(describe("GMANMod", 3.0f, 3.0f), GMANMod(3.0f, 3.0f));
  expectZeroValued(describe("GMANMod", 5.0f, -5.0f), GMANMod(5.0f, -5.0f));

  for (RtFloat const zero : {0.0f, -0.0f}) {
    expectNaN(describe("GMANMod", 3.0f, zero), GMANMod(3.0f, zero));
    expectNaN(describe("GMANMod", -3.0f, zero), GMANMod(-3.0f, zero));
    expectNaN(describe("GMANMod", 0.0f, zero), GMANMod(0.0f, zero));
  }

  expectNaN(describe("GMANMod", kNaN, 2.0f), GMANMod(kNaN, 2.0f));
  expectNaN(describe("GMANMod", 2.0f, kNaN), GMANMod(2.0f, kNaN));
  expectNaN(describe("GMANMod", -2.0f, kNaN), GMANMod(-2.0f, kNaN));
  expectNaN(describe("GMANMod", kNaN, kNaN), GMANMod(kNaN, kNaN));

  observe("GMANMod", GMANMod(3, kInf), 3, kInf);
  observe("GMANMod", GMANMod(3, -kInf), 3, -kInf);
  observe("GMANMod", GMANMod(-3, kInf), -3, kInf);
  observe("GMANMod", GMANMod(-3, -kInf), -3, -kInf);
  observe("GMANMod", GMANMod(kInf, 2), kInf, 2);
  observe("GMANMod", GMANMod(-kInf, 2), -kInf, 2);
  observe("GMANMod", GMANMod(kInf, kInf), kInf, kInf);
  observe("GMANMod", GMANMod(-0.0f, 2), -0.0f, 2);
  observe("GMANMod", GMANMod(0.0f, 2), 0.0f, 2);
  observe("GMANMod", GMANMod(4, 2), 4, 2);
}

void checkSign() {
  struct Signed {
    RtFloat x;
    RtFloat expected;
  };
  const std::array signs = {Signed{-2, -1},  Signed{3, 1},      Signed{0.25f, 1},      Signed{-0.25f, -1},
                            Signed{kInf, 1}, Signed{-kInf, -1}, Signed{kDenormMin, 1}, Signed{-kDenormMin, -1},
                            Signed{kMax, 1}, Signed{-kMax, -1}};
  for (auto const& [x, expected] : signs) {
    expectExactly(describe("GMANSign", x), GMANSign(x), expected);
  }

  expectZeroValued(describe("GMANSign", 0.0f), GMANSign(0.0f));
  expectZeroValued(describe("GMANSign", -0.0f), GMANSign(-0.0f));
  observe("GMANSign", GMANSign(0.0f), 0.0f);
  observe("GMANSign", GMANSign(-0.0f), -0.0f);
}

template <class T> void checkMixAs(std::string const& type) {
  const std::string name = std::format("GMANMix<{}>", type);
  struct Blend {
    T a;
    T b;
    RtFloat alpha;
    T expected;
  };
  const std::array blends = {Blend{2, 4, 0.5f, 3},
                             Blend{2, 4, 0, 2},
                             Blend{2, 4, 1, 4},
                             Blend{0, 8, 0.25f, 2},
                             Blend{1, 3, 2, 5},
                             Blend{1, 3, -1, -1},
                             Blend{-2, 2, 0.75f, 1},
                             Blend{10, 20, 0.5f, 15},
                             Blend{static_cast<T>(kInf), 1, 0.5f, static_cast<T>(kInf)},
                             Blend{1, static_cast<T>(-kInf), 0.5f, static_cast<T>(-kInf)}};
  for (auto const& [a, b, alpha, expected] : blends) {
    expectExactly(describe(name, a, b, alpha), GMANMix(a, b, alpha), expected);
  }

  const T nan = std::numeric_limits<T>::quiet_NaN();
  expectNaN(describe(name, nan, 1, 0.5f), GMANMix(nan, T{1}, 0.5f));
  expectNaN(describe(name, 1, nan, 0.5f), GMANMix(T{1}, nan, 0.5f));
  expectNaN(describe(name, 1, 2, kNaN), GMANMix(T{1}, T{2}, kNaN));
}

void checkMix() {
  checkMixAs<RtFloat>("float");
  checkMixAs<double>("double");
  expectExactly(describe("GMANMix<double>", 16777217.0, 0.0, 0.5f), GMANMix(16777217.0, 0.0, 0.5f), 8388608.5);
}

void checkRound() {
  struct Rounded {
    RtFloat x;
    RtFloat expected;
  };
  const std::array rounded = {
      Rounded{1.2f, 1},      Rounded{1.7f, 2},           Rounded{-1.2f, -1},           Rounded{-1.7f, -2},
      Rounded{2.4f, 2},      Rounded{2.6f, 3},           Rounded{-2.6f, -3},           Rounded{0.7f, 1},
      Rounded{-0.7f, -1},    Rounded{0x1.000002p-1f, 1}, Rounded{-0x1.000002p-1f, -1}, Rounded{8388609, 8388609},
      Rounded{1e10f, 1e10f}, Rounded{kMax, kMax},        Rounded{kInf, kInf},          Rounded{-kInf, -kInf}};
  for (auto const& [x, expected] : rounded) {
    expectExactly(describe("GMANRound", x), GMANRound(x), expected);
  }

  expectZeroValued(describe("GMANRound", 0.3f), GMANRound(0.3f));
  expectZeroValued(describe("GMANRound", -0.3f), GMANRound(-0.3f));
  expectZeroValued(describe("GMANRound", 0x1.fffffep-2f), GMANRound(0x1.fffffep-2f));
  expectZeroValued(describe("GMANRound", 0.0f), GMANRound(0.0f));
  expectZeroValued(describe("GMANRound", -0.0f), GMANRound(-0.0f));
  expectNaN(describe("GMANRound", kNaN), GMANRound(kNaN));

  for (RtFloat const tie : {0.5f, -0.5f, 1.5f, -1.5f, 2.5f, -2.5f}) {
    observe("GMANRound", GMANRound(tie), tie);
  }
  observe("GMANRound", GMANRound(-0.3f), -0.3f);
  observe("GMANRound", GMANRound(-0.0f), -0.0f);
}

} // namespace

int main() {
  checkRadiansAndDegrees();
  checkAtan();
  checkInversesqrt();
  checkLogFn();
  checkMod();
  checkSign();
  checkMix();
  checkRound();
  return checkSummary("gmanmath.h's shading language functions meet their definitions and IEEE edges");
}
