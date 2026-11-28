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
 * gman::HPoint<T> for RtFloat and gman::PartnerReal, one named check per member
 * and instance: constructors, copies and copies through std::move, w, the Point
 * members it inherits, wToOne and the matrix product. A failure names the
 * member, the instance, the component, and the expected and actual values. The
 * sum and the scaling, and their compound forms, add to a temporary rather than
 * to the point; the values they should produce are pinned by the hpointadd*_pin
 * and hpointscale*_pin tests. Here they are checked for what they return, what
 * they leave alone and the w they produce.
 */

#include <string>
#include <utility>

#include "check.h"
#include "geometrycheck.h"
#include "gmanhpoint.h"
#include "gmanmatrix4.h"
#include "gmanreal.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> std::string label(std::string const& name) { return member<T>("HPoint", name); }

template <typename T> void testDefaultConstructor() {
  const auto h = gman::HPoint<T>();
  expectExact(label<T>("HPoint() has w equal to 1"), h, {0, 0, 0, 1});
}

template <typename T> void testComponentConstructor() {
  const auto h = gman::HPoint<T>(1, 2, 3, 4);
  expectExact(label<T>("HPoint(x, y, z, w)"), h, {1, 2, 3, 4});
}

template <typename T> void testPointerConstructorFromRtFloat() {
  constexpr RtFloat parts[4] = {4.5, 5.25, 6.125, 7.0625};
  const auto v = gman::HPoint<T>(parts);
  expectExact(label<T>("HPoint(RtFloat const*)"), v, {4.5, 5.25, 6.125, 7.0625});
}

template <typename T> void testPointerConstructorFromPartnerReal() {
  constexpr gman::PartnerReal parts[4] = {4.5, 5.25, 6.125, 7.0625};
  const auto v = gman::HPoint<T>(parts);
  expectExact(label<T>("HPoint(PartnerReal const*)"), v, {4.5, 5.25, 6.125, 7.0625});
}

template <typename T> void testConvertingConstructor() {
  using U = OtherInstance<T>;
  const auto source = gman::HPoint<U>(U(1) / 3, U(2) / 7, U(-5) / 11, U(13) / 17);
  const auto converted = gman::HPoint<T>(source);
  expectExact(label<T>("HPoint(HPoint<U> const&)"), converted,
              {static_cast<T>(source.getX()), static_cast<T>(source.getY()), static_cast<T>(source.getZ()),
               static_cast<T>(source.getW())});
}

template <typename T> void testCopyConstruct() {
  const auto source = gman::HPoint<T>(1, 2, 3, 4);
  const auto copy = source;
  expectExact(label<T>("HPoint(HPoint const&)"), copy, {1, 2, 3, 4});
}

template <typename T> void testCopyAssign() {
  const auto source = gman::HPoint<T>(1, 2, 3, 4);
  auto target = gman::HPoint<T>(9, 9, 9, 9);
  auto const& result = (target = source);
  expectExact(label<T>("operator=(HPoint const&)"), target, {1, 2, 3, 4});
  expectBool(label<T>("operator=(HPoint const&) returns *this"), &result == &target, true);
}

template <typename T> void testCopyConstructViaStdMove() {
  auto source = gman::HPoint<T>(1, 2, 3, 4);
  const auto moved = std::move(source);
  expectExact(label<T>("HPoint(HPoint const&) via std::move"), moved, {1, 2, 3, 4});
}

template <typename T> void testCopyAssignViaStdMove() {
  auto source = gman::HPoint<T>(1, 2, 3, 4);
  auto target = gman::HPoint<T>(9, 9, 9, 9);
  target = std::move(source);
  expectExact(label<T>("operator=(HPoint const&) via std::move"), target, {1, 2, 3, 4});
}

template <typename T> void testGetters() {
  const auto h = gman::HPoint<T>(1, 2, 3, 4);
  expectExact(label<T>("getX"), h.getX(), 1);
  expectExact(label<T>("getY"), h.getY(), 2);
  expectExact(label<T>("getZ"), h.getZ(), 3);
  expectExact(label<T>("getW"), h.getW(), 4);
}

template <typename T> void testSetters() {
  auto atX = gman::HPoint<T>(1, 2, 3, 4);
  auto atY = gman::HPoint<T>(1, 2, 3, 4);
  auto atZ = gman::HPoint<T>(1, 2, 3, 4);
  auto atW = gman::HPoint<T>(1, 2, 3, 4);
  atX.setX(10);
  atY.setY(10);
  atZ.setZ(10);
  atW.setW(10);
  expectExact(label<T>("setX"), atX, {10, 2, 3, 4});
  expectExact(label<T>("setY"), atY, {1, 10, 3, 4});
  expectExact(label<T>("setZ"), atZ, {1, 2, 10, 4});
  expectExact(label<T>("setW"), atW, {1, 2, 3, 10});
}

template <typename T> void testIndexRead() {
  const auto h = gman::HPoint<T>(1, 2, 3, 4);
  expectExact(label<T>("operator[](0)"), h[0], 1);
  expectExact(label<T>("operator[](1)"), h[1], 2);
  expectExact(label<T>("operator[](2)"), h[2], 3);
}

template <typename T> void testWToOne() {
  auto h = gman::HPoint<T>(2, 4, 6, 2);
  h.wToOne();
  expectExact(label<T>("wToOne by 2"), h, {1, 2, 3, 1});
}

template <typename T> void testWToOneByFour() {
  auto h = gman::HPoint<T>(2, 4, 6, 4);
  h.wToOne();
  expectExact(label<T>("wToOne by 4"), h, {T(1) / 2, 1, T(3) / 2, 1});
}

template <typename T> void testWToOneRoundsAtItsOwnPrecision() {
  auto h = gman::HPoint<T>(1, 2, 3, 3);
  h.wToOne();
  expectClose(label<T>("wToOne by 3"), h, {T(1) / 3, T(2) / 3, 1, 1}, 2);
}

template <typename T> void testMatrixMultiplyAssign() {
  auto h = gman::HPoint<T>(1, 2, 3, 4);
  auto const& result = (h *= sequentialMatrix<T>());
  expectExact(label<T>("operator*=(Matrix4) applies the matrix to a row"), h, {90, 100, 110, 120});
  expectBool(label<T>("operator*=(Matrix4) returns *this"), &result == &h, true);
}

template <typename T> void testMatrixMultiplyAssignTranslates() {
  auto h = gman::HPoint<T>(1, 2, 3, 1);
  auto translation = gman::Matrix4<T>();
  translation.trans(10, 20, 30);
  h *= translation;
  expectExact(label<T>("operator*=(Matrix4) by a translation"), h, {11, 22, 33, 1});
}

template <typename T> void testMatrixMultiply() {
  const auto h = gman::HPoint<T>(1, 2, 3, 4);
  const auto product = h * sequentialMatrix<T>();
  expectExact(label<T>("operator*(Matrix4)"), product, {90, 100, 110, 120});
}

template <typename T> void testMatrixMultiplyLeavesItsOperand() {
  const auto h = gman::HPoint<T>(1, 2, 3, 4);
  static_cast<void>(h * sequentialMatrix<T>());
  expectExact(label<T>("operator*(Matrix4) leaves its operand"), h, {1, 2, 3, 4});
}

template <typename T> void testAddAssignReturnsSelf() {
  auto h = gman::HPoint<T>(1, 2, 3, 1);
  auto const& result = (h += gman::HPoint<T>(10, 20, 40, 1));
  expectBool(label<T>("operator+=(HPoint) returns *this"), &result == &h, true);
}

template <typename T> void testAddLeavesItsOperands() {
  const auto a = gman::HPoint<T>(1, 2, 3, 4);
  const auto b = gman::HPoint<T>(10, 20, 40, 80);
  static_cast<void>(a + b);
  expectExact(label<T>("operator+(HPoint) leaves the left operand"), a, {1, 2, 3, 4});
  expectExact(label<T>("operator+(HPoint) leaves the right operand"), b, {10, 20, 40, 80});
}

template <typename T> void testMultiplyAssignScalarReturnsSelf() {
  auto h = gman::HPoint<T>(1, 2, 3, 1);
  auto const& result = (h *= T(4));
  expectBool(label<T>("operator*=(T) returns *this"), &result == &h, true);
}

template <typename T> void testMultiplyScalarLeavesItsOperand() {
  const auto h = gman::HPoint<T>(1, 2, 3, 4);
  static_cast<void>(h * T(2));
  expectExact(label<T>("operator*(T) leaves its operand"), h, {1, 2, 3, 4});
}

template <typename T> void testMatrixMultiplyAssignRoundsAtItsOwnPrecision() {
  auto h = gman::HPoint<T>(reciprocal<T, 3>, reciprocal<T, 7>, reciprocal<T, 11>, 1);
  h *= rowWitnessMatrix<T>();
  expectClose(label<T>("operator*=(Matrix4) of non-dyadic entries"), h, {T(14) / 45, T(62) / 637, T(138) / 2057, 1}, 4);
}

template <typename T> void testMatrixMultiplyRoundsAtItsOwnPrecision() {
  const auto h = gman::HPoint<T>(reciprocal<T, 3>, reciprocal<T, 7>, reciprocal<T, 11>, 1);
  expectClose(label<T>("operator*(Matrix4) of non-dyadic entries"), h * rowWitnessMatrix<T>(),
              {T(14) / 45, T(62) / 637, T(138) / 2057, 1}, 4);
}

template <typename T> void testAddAssignAddsTheWComponents() {
  auto h = gman::HPoint<T>(1, 2, 3, 1);
  h += gman::HPoint<T>(10, 20, 40, 5);
  expectExact(label<T>("operator+=(HPoint) adds the w components"), h.getW(), 6);
}

template <typename T> void testAddAddsTheWComponents() {
  const auto sum = gman::HPoint<T>(1, 2, 3, 1) + gman::HPoint<T>(10, 20, 40, 5);
  expectExact(label<T>("operator+(HPoint) adds the w components"), sum.getW(), 6);
}

template <typename T> void testMultiplyAssignScalarScalesW() {
  auto h = gman::HPoint<T>(1, 2, 3, 1);
  h *= T(4);
  expectExact(label<T>("operator*=(T) scales w"), h.getW(), 4);
}

template <typename T> void testMultiplyScalarScalesW() {
  const auto scaled = gman::HPoint<T>(1, 2, 3, 1) * T(4);
  expectExact(label<T>("operator*(T) scales w"), scaled.getW(), 4);
}

template <typename T> void runAll() {
  testDefaultConstructor<T>();
  testComponentConstructor<T>();
  testPointerConstructorFromRtFloat<T>();
  testPointerConstructorFromPartnerReal<T>();
  testConvertingConstructor<T>();
  testCopyConstruct<T>();
  testCopyAssign<T>();
  testCopyConstructViaStdMove<T>();
  testCopyAssignViaStdMove<T>();
  testGetters<T>();
  testSetters<T>();
  testIndexRead<T>();
  testWToOne<T>();
  testWToOneByFour<T>();
  testWToOneRoundsAtItsOwnPrecision<T>();
  testMatrixMultiplyAssign<T>();
  testMatrixMultiplyAssignTranslates<T>();
  testMatrixMultiply<T>();
  testMatrixMultiplyLeavesItsOperand<T>();
  testAddAssignReturnsSelf<T>();
  testAddLeavesItsOperands<T>();
  testMultiplyAssignScalarReturnsSelf<T>();
  testMultiplyScalarLeavesItsOperand<T>();
  testMatrixMultiplyAssignRoundsAtItsOwnPrecision<T>();
  testMatrixMultiplyRoundsAtItsOwnPrecision<T>();
  testAddAssignAddsTheWComponents<T>();
  testAddAddsTheWComponents<T>();
  testMultiplyAssignScalarScalesW<T>();
  testMultiplyScalarScalesW<T>();
}

} // namespace

int main() {
  runAll<RtFloat>();
  runAll<gman::PartnerReal>();
  return checkSummary("hpointtemplate: all checks passed");
}
