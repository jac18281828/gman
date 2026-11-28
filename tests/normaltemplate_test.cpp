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
 * gman::Normal<T> for RtFloat and gman::PartnerReal, one named check per
 * member and instance: its own constructors, copies and copies through std::move, and the
 * Vector members it inherits: accessors, indexing, arithmetic, dot, cross,
 * magnitude and normalize. A failure names the member, the instance, the
 * component, and the expected and actual values.
 */

#include <numbers>
#include <string>
#include <utility>

#include "check.h"
#include "geometrycheck.h"
#include "gmannormal.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> std::string label(std::string const& name) { return member<T>("Normal", name); }

template <typename T> void testDefaultConstructor() {
  const auto n = gman::Normal<T>();
  expectExact(label<T>("Normal()"), n, {0, 0, 0});
}

template <typename T> void testComponentConstructor() {
  const auto n = gman::Normal<T>(1, 2, 3);
  expectExact(label<T>("Normal(x, y, z)"), n, {1, 2, 3});
}

template <typename T> void testPointConstructor() {
  const auto n = gman::Normal<T>(gman::Point<T>(1, 2, 3));
  expectExact(label<T>("Normal(Point const&)"), n, {1, 2, 3});
}

template <typename T> void testPointPairConstructor() {
  const auto n = gman::Normal<T>(gman::Point<T>(1, 2, 3), gman::Point<T>(5, 9, 14));
  expectExact(label<T>("Normal(Point a, Point b) is b - a"), n, {4, 7, 11});
}

template <typename T> void testConvertingConstructor() {
  using U = OtherInstance<T>;
  const auto source = gman::Normal<U>(U(1) / 3, U(2) / 7, U(-5) / 11);
  const auto converted = gman::Normal<T>(source);
  expectExact(label<T>("Normal(Normal<U> const&)"), converted,
              {static_cast<T>(source.getX()), static_cast<T>(source.getY()), static_cast<T>(source.getZ())});
}

template <typename T> void testCopyConstruct() {
  const auto source = gman::Normal<T>(1, 2, 3);
  const auto copy = source;
  expectExact(label<T>("Normal(Normal const&)"), copy, {1, 2, 3});
}

template <typename T> void testCopyAssign() {
  const auto source = gman::Normal<T>(1, 2, 3);
  auto target = gman::Normal<T>(9, 9, 9);
  auto const& result = (target = source);
  expectExact(label<T>("operator=(Normal const&)"), target, {1, 2, 3});
  expectBool(label<T>("operator=(Normal const&) returns *this"), &result == &target, true);
}

template <typename T> void testCopyConstructViaStdMove() {
  auto source = gman::Normal<T>(1, 2, 3);
  const auto moved = std::move(source);
  expectExact(label<T>("Normal(Normal const&) via std::move"), moved, {1, 2, 3});
}

template <typename T> void testCopyAssignViaStdMove() {
  auto source = gman::Normal<T>(1, 2, 3);
  auto target = gman::Normal<T>(9, 9, 9);
  target = std::move(source);
  expectExact(label<T>("operator=(Normal const&) via std::move"), target, {1, 2, 3});
}

template <typename T> void testGetters() {
  const auto n = gman::Normal<T>(1, 2, 3);
  expectExact(label<T>("getX"), n.getX(), 1);
  expectExact(label<T>("getY"), n.getY(), 2);
  expectExact(label<T>("getZ"), n.getZ(), 3);
}

template <typename T> void testSetters() {
  auto atX = gman::Normal<T>(1, 2, 3);
  auto atY = gman::Normal<T>(1, 2, 3);
  auto atZ = gman::Normal<T>(1, 2, 3);
  atX.setX(10);
  atY.setY(10);
  atZ.setZ(10);
  expectExact(label<T>("setX"), atX, {10, 2, 3});
  expectExact(label<T>("setY"), atY, {1, 10, 3});
  expectExact(label<T>("setZ"), atZ, {1, 2, 10});
}

template <typename T> void testIndexRead() {
  auto n = gman::Normal<T>(1, 2, 3);
  expectExact(label<T>("operator[](0)"), n[0], 1);
  expectExact(label<T>("operator[](1)"), n[1], 2);
  expectExact(label<T>("operator[](2)"), n[2], 3);
}

template <typename T> void testIndexReadConst() {
  const auto n = gman::Normal<T>(1, 2, 3);
  expectExact(label<T>("operator[](0) const"), n[0], 1);
  expectExact(label<T>("operator[](1) const"), n[1], 2);
  expectExact(label<T>("operator[](2) const"), n[2], 3);
}

template <typename T> void testIndexWrite() {
  auto atX = gman::Normal<T>(1, 2, 3);
  auto atY = gman::Normal<T>(1, 2, 3);
  auto atZ = gman::Normal<T>(1, 2, 3);
  atX[0] = 10;
  atY[1] = 10;
  atZ[2] = 10;
  expectExact(label<T>("operator[](0) writes x"), atX, {10, 2, 3});
  expectExact(label<T>("operator[](1) writes y"), atY, {1, 10, 3});
  expectExact(label<T>("operator[](2) writes z"), atZ, {1, 2, 10});
}

template <typename T> void testAddition() {
  const auto sum = gman::Normal<T>(1, 2, 3) + gman::Normal<T>(10, 20, 40);
  expectExact(label<T>("operator+ yields a Vector sum"), sum, {11, 22, 43});
}

template <typename T> void testSubtraction() {
  const auto difference = gman::Normal<T>(10, 20, 40) - gman::Normal<T>(1, 2, 3);
  expectExact(label<T>("operator- yields a Vector difference"), difference, {9, 18, 37});
}

template <typename T> void testUnaryMinus() {
  const auto negated = -gman::Normal<T>(1, -2, 3);
  expectExact(label<T>("operator-()"), negated, {-1, 2, -3});
}

template <typename T> void testMultiplyScalar() {
  const auto scaled = gman::Normal<T>(1, 2, 3) * T(4);
  expectExact(label<T>("operator*(T)"), scaled, {4, 8, 12});
}

template <typename T> void testDivideScalar() {
  const auto scaled = gman::Normal<T>(1, 2, 3) / T(2);
  expectExact(label<T>("operator/(T)"), scaled, {T(1) / 2, 1, T(3) / 2});
}

template <typename T> void testDot() {
  const auto d = gman::Normal<T>(1, 2, 3).dot(gman::Vector<T>(4, 5, 6));
  expectExact(label<T>("dot"), d, 32);
}

template <typename T> void testCross() {
  const auto c = gman::Normal<T>(1, 2, 3).cross(gman::Vector<T>(4, 5, 6));
  expectExact(label<T>("cross"), c, {-3, 6, -3});
}

template <typename T> void testMagnitude() {
  auto n = gman::Normal<T>(3, 4, 12);
  expectExact(label<T>("magnitude"), n.magnitude(), 13);
}

template <typename T> void testNormalize() {
  auto n = gman::Normal<T>(0, 3, 4);
  n.normalize();
  expectClose(label<T>("normalize"), n, {0, T(3) / 5, T(4) / 5}, 2);
}

template <typename T> void testNormalizeRoundsAtItsOwnPrecision() {
  auto n = gman::Normal<T>(1, 1, 1);
  n.normalize();
  constexpr T unit = std::numbers::inv_sqrt3_v<T>;
  expectClose(label<T>("normalize of (1, 1, 1)"), n, {unit, unit, unit}, 4);
}

template <typename T> void testNormalizeLeavesTheZeroNormal() {
  auto n = gman::Normal<T>(0, 0, 0);
  n.normalize();
  expectExact(label<T>("normalize of the zero normal"), n, {0, 0, 0});
}

template <typename T> void runAll() {
  testDefaultConstructor<T>();
  testComponentConstructor<T>();
  testPointConstructor<T>();
  testPointPairConstructor<T>();
  testConvertingConstructor<T>();
  testCopyConstruct<T>();
  testCopyAssign<T>();
  testCopyConstructViaStdMove<T>();
  testCopyAssignViaStdMove<T>();
  testGetters<T>();
  testSetters<T>();
  testIndexRead<T>();
  testIndexReadConst<T>();
  testIndexWrite<T>();
  testAddition<T>();
  testSubtraction<T>();
  testUnaryMinus<T>();
  testMultiplyScalar<T>();
  testDivideScalar<T>();
  testDot<T>();
  testCross<T>();
  testMagnitude<T>();
  testNormalize<T>();
  testNormalizeRoundsAtItsOwnPrecision<T>();
  testNormalizeLeavesTheZeroNormal<T>();
}

} // namespace

int main() {
  runAll<RtFloat>();
  runAll<gman::PartnerReal>();
  return checkSummary("normaltemplate: all checks passed");
}
