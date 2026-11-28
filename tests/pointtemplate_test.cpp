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
 * gman::Point<T> for RtFloat and gman::PartnerReal, one named check per
 * member and instance: constructors, copies and moves, accessors, the const
 * indexing, comparison, addition, scaling and the matrix transform. A failure
 * names the member, the instance, the component, and the expected and actual
 * values. The constructor from an HPoint is checked for its value here; the
 * argument it mutates is pinned by pointfromhpoint_pin_test.cpp.
 */

#include <string>
#include <type_traits>
#include <utility>

#include "check.h"
#include "geometrycheck.h"
#include "gmanhpoint.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> std::string label(std::string const& name) { return member<T>("Point", name); }

template <typename T> void testDefaultConstructor() {
  const auto p = gman::Point<T>();
  expectExact(label<T>("Point()"), p, {0, 0, 0});
}

template <typename T> void testComponentConstructor() {
  const auto p = gman::Point<T>(1, 2, 3);
  expectExact(label<T>("Point(x, y, z)"), p, {1, 2, 3});
}

template <typename T> void testDefaultedArguments() {
  const auto onlyX = gman::Point<T>(1);
  const auto xAndY = gman::Point<T>(1, 2);
  expectExact(label<T>("Point(x) defaults y and z to 0"), onlyX, {1, 0, 0});
  expectExact(label<T>("Point(x, y) defaults z to 0"), xAndY, {1, 2, 0});
}

template <typename T> void testPointerConstructor() {
  if constexpr (std::is_same_v<T, RtFloat>) {
    constexpr RtFloat parts[3] = {4, 5, 6};
    const auto p = gman::Point<T>(parts);
    expectExact(label<T>("Point(RtFloat const*)"), p, {4, 5, 6});
  }
}

template <typename T> void testConvertingConstructor() {
  using U = OtherInstance<T>;
  const auto source = gman::Point<U>(U(1) / 3, U(2) / 7, U(-5) / 11);
  const auto converted = gman::Point<T>(source);
  expectExact(label<T>("Point(Point<U> const&)"), converted,
              {static_cast<T>(source.getX()), static_cast<T>(source.getY()), static_cast<T>(source.getZ())});
}

template <typename T> void testConstructFromHPoint() {
  auto h = gman::HPoint<T>(2, 4, 6, 2);
  const auto p = gman::Point<T>(h);
  expectExact(label<T>("Point(HPoint&) divides by w"), p, {1, 2, 3});
}

template <typename T> void testCopyConstruct() {
  const auto source = gman::Point<T>(1, 2, 3);
  const auto copy = source;
  expectExact(label<T>("Point(Point const&)"), copy, {1, 2, 3});
}

template <typename T> void testCopyAssign() {
  const auto source = gman::Point<T>(1, 2, 3);
  auto target = gman::Point<T>(9, 9, 9);
  auto& result = (target = source);
  expectExact(label<T>("operator=(Point const&)"), target, {1, 2, 3});
  expectBool(label<T>("operator=(Point const&) returns *this"), &result == &target, true);
}

template <typename T> void testMoveConstruct() {
  auto source = gman::Point<T>(1, 2, 3);
  const auto moved = std::move(source);
  expectExact(label<T>("Point(Point&&)"), moved, {1, 2, 3});
}

template <typename T> void testMoveAssign() {
  auto source = gman::Point<T>(1, 2, 3);
  auto target = gman::Point<T>(9, 9, 9);
  target = std::move(source);
  expectExact(label<T>("operator=(Point&&)"), target, {1, 2, 3});
}

template <typename T> void testGetters() {
  const auto p = gman::Point<T>(1, 2, 3);
  expectExact(label<T>("getX"), p.getX(), 1);
  expectExact(label<T>("getY"), p.getY(), 2);
  expectExact(label<T>("getZ"), p.getZ(), 3);
}

template <typename T> void testSetX() {
  auto p = gman::Point<T>(1, 2, 3);
  p.setX(10);
  expectExact(label<T>("setX"), p, {10, 2, 3});
}

template <typename T> void testSetY() {
  auto p = gman::Point<T>(1, 2, 3);
  p.setY(10);
  expectExact(label<T>("setY"), p, {1, 10, 3});
}

template <typename T> void testSetZ() {
  auto p = gman::Point<T>(1, 2, 3);
  p.setZ(10);
  expectExact(label<T>("setZ"), p, {1, 2, 10});
}

template <typename T> void testIndexRead() {
  const auto p = gman::Point<T>(1, 2, 3);
  expectExact(label<T>("operator[](0)"), p[0], 1);
  expectExact(label<T>("operator[](1)"), p[1], 2);
  expectExact(label<T>("operator[](2)"), p[2], 3);
}

template <typename T> void testIndexReadOnNonConstPoint() {
  auto p = gman::Point<T>(1, 2, 3);
  expectExact(label<T>("operator[](0) on a non-const Point"), p[0], 1);
  expectExact(label<T>("operator[](1) on a non-const Point"), p[1], 2);
  expectExact(label<T>("operator[](2) on a non-const Point"), p[2], 3);
}

template <typename T> void testAssignScalar() {
  auto p = gman::Point<T>(1, 2, 3);
  auto& result = (p = T(7));
  expectExact(label<T>("operator=(T)"), p, {7, 7, 7});
  expectBool(label<T>("operator=(T) returns *this"), &result == &p, true);
}

template <typename T> void testEqualityHolds() {
  expectBool(label<T>("operator== of equal points"), gman::Point<T>(1, 2, 3) == gman::Point<T>(1, 2, 3), true);
}

template <typename T> void testEqualityFailsWhenAComponentDiffers() {
  const auto p = gman::Point<T>(1, 2, 3);
  expectBool(label<T>("operator== differing in x"), p == gman::Point<T>(9, 2, 3), false);
  expectBool(label<T>("operator== differing in y"), p == gman::Point<T>(1, 9, 3), false);
  expectBool(label<T>("operator== differing in z"), p == gman::Point<T>(1, 2, 9), false);
}

template <typename T> void testInequalityFailsForEqualPoints() {
  expectBool(label<T>("operator!= of equal points"), gman::Point<T>(1, 2, 3) != gman::Point<T>(1, 2, 3), false);
}

template <typename T> void testInequalityHoldsWhenAComponentDiffers() {
  const auto p = gman::Point<T>(1, 2, 3);
  expectBool(label<T>("operator!= differing in x"), p != gman::Point<T>(9, 2, 3), true);
  expectBool(label<T>("operator!= differing in y"), p != gman::Point<T>(1, 9, 3), true);
  expectBool(label<T>("operator!= differing in z"), p != gman::Point<T>(1, 2, 9), true);
}

template <typename T> void testLessThanHoldsWhenEveryComponentIsLess() {
  expectBool(label<T>("operator< with every component less"), gman::Point<T>(1, 2, 3) < gman::Point<T>(2, 3, 4), true);
}

template <typename T> void testLessThanFailsWhenAComponentIsNotLess() {
  const auto p = gman::Point<T>(1, 2, 3);
  expectBool(label<T>("operator< with x not less"), p < gman::Point<T>(1, 3, 4), false);
  expectBool(label<T>("operator< with y not less"), p < gman::Point<T>(2, 2, 4), false);
  expectBool(label<T>("operator< with z not less"), p < gman::Point<T>(2, 3, 3), false);
}

template <typename T> void testAddAssign() {
  auto p = gman::Point<T>(1, 2, 3);
  auto& result = (p += gman::Point<T>(10, 20, 40));
  expectExact(label<T>("operator+=(Point)"), p, {11, 22, 43});
  expectBool(label<T>("operator+=(Point) returns *this"), &result == &p, true);
}

template <typename T> void testAddition() {
  const auto sum = gman::Point<T>(1, 2, 3) + gman::Point<T>(10, 20, 40);
  expectExact(label<T>("operator+(Point)"), sum, {11, 22, 43});
}

template <typename T> void testAdditionLeavesItsOperands() {
  const auto a = gman::Point<T>(1, 2, 3);
  const auto b = gman::Point<T>(10, 20, 40);
  static_cast<void>(a + b);
  expectExact(label<T>("operator+(Point) leaves the left operand"), a, {1, 2, 3});
  expectExact(label<T>("operator+(Point) leaves the right operand"), b, {10, 20, 40});
}

template <typename T> void testMultiplyAssignScalar() {
  auto p = gman::Point<T>(1, 2, 3);
  auto& result = (p *= T(4));
  expectExact(label<T>("operator*=(T)"), p, {4, 8, 12});
  expectBool(label<T>("operator*=(T) returns *this"), &result == &p, true);
}

template <typename T> void testMultiplyScalar() {
  const auto scaled = gman::Point<T>(1, 2, 3) * T(4);
  expectExact(label<T>("operator*(T)"), scaled, {4, 8, 12});
}

template <typename T> void testMultiplyScalarLeavesItsOperand() {
  const auto p = gman::Point<T>(1, 2, 3);
  static_cast<void>(p * T(4));
  expectExact(label<T>("operator*(T) leaves its operand"), p, {1, 2, 3});
}

template <typename T> void testMatrixMultiplyAssign() {
  auto p = gman::Point<T>(1, 2, 3);
  auto& result = (p *= sequentialMatrix<T>());
  expectExact(label<T>("operator*=(Matrix4) applies the matrix to a column"), p, {18, 46, 74});
  expectBool(label<T>("operator*=(Matrix4) returns *this"), &result == &p, true);
}

template <typename T> void runAll() {
  testDefaultConstructor<T>();
  testComponentConstructor<T>();
  testDefaultedArguments<T>();
  testPointerConstructor<T>();
  testConvertingConstructor<T>();
  testConstructFromHPoint<T>();
  testCopyConstruct<T>();
  testCopyAssign<T>();
  testMoveConstruct<T>();
  testMoveAssign<T>();
  testGetters<T>();
  testSetX<T>();
  testSetY<T>();
  testSetZ<T>();
  testIndexRead<T>();
  testIndexReadOnNonConstPoint<T>();
  testAssignScalar<T>();
  testEqualityHolds<T>();
  testEqualityFailsWhenAComponentDiffers<T>();
  testInequalityFailsForEqualPoints<T>();
  testInequalityHoldsWhenAComponentDiffers<T>();
  testLessThanHoldsWhenEveryComponentIsLess<T>();
  testLessThanFailsWhenAComponentIsNotLess<T>();
  testAddAssign<T>();
  testAddition<T>();
  testAdditionLeavesItsOperands<T>();
  testMultiplyAssignScalar<T>();
  testMultiplyScalar<T>();
  testMultiplyScalarLeavesItsOperand<T>();
  testMatrixMultiplyAssign<T>();
}

} // namespace

int main() {
  runAll<RtFloat>();
  runAll<gman::PartnerReal>();
  return checkSummary("pointtemplate: all checks passed");
}
