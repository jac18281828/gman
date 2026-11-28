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
 * gman::Vector4<T> for RtFloat and gman::PartnerReal, one named check per
 * member and instance: constructors, copies and moves, w, the Vector members
 * it inherits, arithmetic over all four components, magnitude, normalize,
 * dot, the matrix product, the perspective division and, on the RtFloat
 * instance alone, the projective transform by an RtMatrix. A failure names
 * the member, the instance, the component, and the expected and actual
 * values. The default constructor's w is pinned by vector4default_pin_test.cpp.
 */

#include <numbers>
#include <string>
#include <type_traits>
#include <utility>

#include "check.h"
#include "geometrycheck.h"
#include "gmanhpoint.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "gmanvector4.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> std::string label(std::string const& name) { return member<T>("Vector4", name); }

template <typename T> void testDefaultConstructorZeroesTheVectorPart() {
  const auto v = gman::Vector4<T>();
  expectExact(label<T>("Vector4() zeroes x"), v.getX(), 0);
  expectExact(label<T>("Vector4() zeroes y"), v.getY(), 0);
  expectExact(label<T>("Vector4() zeroes z"), v.getZ(), 0);
}

template <typename T> void testComponentConstructor() {
  const auto v = gman::Vector4<T>(1, 2, 3, 4);
  expectExact(label<T>("Vector4(x, y, z, w)"), v, {1, 2, 3, 4});
}

template <typename T> void testPointerConstructor() {
  if constexpr (std::is_same_v<T, RtFloat>) {
    constexpr RtFloat parts[4] = {4, 5, 6, 7};
    const auto v = gman::Vector4<T>(parts);
    expectExact(label<T>("Vector4(RtFloat const*)"), v, {4, 5, 6, 7});
  }
}

template <typename T> void testHPointConstructor() {
  const auto v = gman::Vector4<T>(gman::HPoint<T>(1, 2, 3, 4));
  expectExact(label<T>("Vector4(HPoint const&)"), v, {1, 2, 3, 4});
}

template <typename T> void testConvertingConstructor() {
  using U = OtherInstance<T>;
  const auto source = gman::Vector4<U>(U(1) / 3, U(2) / 7, U(-5) / 11, U(13) / 17);
  const auto converted = gman::Vector4<T>(source);
  expectExact(label<T>("Vector4(Vector4<U> const&)"), converted,
              {static_cast<T>(source.getX()), static_cast<T>(source.getY()), static_cast<T>(source.getZ()),
               static_cast<T>(source.getW())});
}

template <typename T> void testCopyConstruct() {
  const auto source = gman::Vector4<T>(1, 2, 3, 4);
  const auto copy = source;
  expectExact(label<T>("Vector4(Vector4 const&)"), copy, {1, 2, 3, 4});
}

template <typename T> void testCopyAssign() {
  const auto source = gman::Vector4<T>(1, 2, 3, 4);
  auto target = gman::Vector4<T>(9, 9, 9, 9);
  auto& result = (target = source);
  expectExact(label<T>("operator=(Vector4 const&)"), target, {1, 2, 3, 4});
  expectBool(label<T>("operator=(Vector4 const&) returns *this"), &result == &target, true);
}

template <typename T> void testMoveConstruct() {
  auto source = gman::Vector4<T>(1, 2, 3, 4);
  const auto moved = std::move(source);
  expectExact(label<T>("Vector4(Vector4&&)"), moved, {1, 2, 3, 4});
}

template <typename T> void testMoveAssign() {
  auto source = gman::Vector4<T>(1, 2, 3, 4);
  auto target = gman::Vector4<T>(9, 9, 9, 9);
  target = std::move(source);
  expectExact(label<T>("operator=(Vector4&&)"), target, {1, 2, 3, 4});
}

template <typename T> void testGetters() {
  const auto v = gman::Vector4<T>(1, 2, 3, 4);
  expectExact(label<T>("getX"), v.getX(), 1);
  expectExact(label<T>("getY"), v.getY(), 2);
  expectExact(label<T>("getZ"), v.getZ(), 3);
  expectExact(label<T>("getW"), v.getW(), 4);
}

template <typename T> void testSetters() {
  auto atX = gman::Vector4<T>(1, 2, 3, 4);
  auto atY = gman::Vector4<T>(1, 2, 3, 4);
  auto atZ = gman::Vector4<T>(1, 2, 3, 4);
  auto atW = gman::Vector4<T>(1, 2, 3, 4);
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
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  expectExact(label<T>("operator[](0)"), v[0], 1);
  expectExact(label<T>("operator[](1)"), v[1], 2);
  expectExact(label<T>("operator[](2)"), v[2], 3);
}

template <typename T> void testIndexWrite() {
  auto atX = gman::Vector4<T>(1, 2, 3, 4);
  auto atY = gman::Vector4<T>(1, 2, 3, 4);
  auto atZ = gman::Vector4<T>(1, 2, 3, 4);
  atX[0] = 10;
  atY[1] = 10;
  atZ[2] = 10;
  expectExact(label<T>("operator[](0) writes x"), atX, {10, 2, 3, 4});
  expectExact(label<T>("operator[](1) writes y"), atY, {1, 10, 3, 4});
  expectExact(label<T>("operator[](2) writes z"), atZ, {1, 2, 10, 4});
}

template <typename T> void testAssignScalar() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  auto& result = (v = T(7));
  expectExact(label<T>("operator=(T) sets all four components"), v, {7, 7, 7, 7});
  expectBool(label<T>("operator=(T) returns *this"), &result == &v, true);
}

template <typename T> void testAssignVectorSetsWToOne() {
  auto v = gman::Vector4<T>(9, 9, 9, 9);
  auto& result = (v = gman::Vector<T>(1, 2, 3));
  expectExact(label<T>("operator=(Vector const&) sets w to 1"), v, {1, 2, 3, 1});
  expectBool(label<T>("operator=(Vector const&) returns *this"), &result == &v, true);
}

template <typename T> void testAddition() {
  const auto sum = gman::Vector4<T>(1, 2, 3, 4) + gman::Vector4<T>(10, 20, 40, 80);
  expectExact(label<T>("operator+(Vector4)"), sum, {11, 22, 43, 84});
}

template <typename T> void testSubtraction() {
  const auto difference = gman::Vector4<T>(10, 20, 40, 80) - gman::Vector4<T>(1, 2, 3, 4);
  expectExact(label<T>("operator-(Vector4)"), difference, {9, 18, 37, 76});
}

template <typename T> void testSubtractAssign() {
  auto v = gman::Vector4<T>(10, 20, 40, 80);
  auto& result = (v -= gman::Vector4<T>(1, 2, 3, 4));
  expectExact(label<T>("operator-=(Vector4)"), v, {9, 18, 37, 76});
  expectBool(label<T>("operator-=(Vector4) returns *this"), &result == &v, true);
}

template <typename T> void testMultiplyAssign() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  auto& result = (v *= gman::Vector4<T>(5, 6, 7, 8));
  expectExact(label<T>("operator*=(Vector4) is componentwise"), v, {5, 12, 21, 32});
  expectBool(label<T>("operator*=(Vector4) returns *this"), &result == &v, true);
}

template <typename T> void testMultiplyAssignScalar() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  auto& result = (v *= T(2));
  expectExact(label<T>("operator*=(T)"), v, {2, 4, 6, 8});
  expectBool(label<T>("operator*=(T) returns *this"), &result == &v, true);
}

template <typename T> void testDivideAssignScalar() {
  auto v = gman::Vector4<T>(2, 4, 6, 8);
  auto& result = (v /= T(2));
  expectExact(label<T>("operator/=(T)"), v, {1, 2, 3, 4});
  expectBool(label<T>("operator/=(T) returns *this"), &result == &v, true);
}

template <typename T> void testMultiplyScalar() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  expectExact(label<T>("operator*(T)"), v * T(2), {2, 4, 6, 8});
}

template <typename T> void testMultiplyScalarLeavesItsOperand() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  static_cast<void>(v * T(2));
  expectExact(label<T>("operator*(T) leaves its operand"), v, {1, 2, 3, 4});
}

template <typename T> void testDivideScalar() {
  auto v = gman::Vector4<T>(2, 4, 6, 8);
  expectExact(label<T>("operator/(T)"), v / T(2), {1, 2, 3, 4});
}

template <typename T> void testDivideScalarRoundsAtItsOwnPrecision() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  expectClose(label<T>("operator/(T) by 3"), v / T(3), {T(1) / 3, T(2) / 3, 1, T(4) / 3}, 1);
}

template <typename T> void testMagnitude() {
  auto v = gman::Vector4<T>(1, 2, 2, 4);
  expectExact(label<T>("magnitude"), v.magnitude(), 5);
}

template <typename T> void testMagnitudeRoundsAtItsOwnPrecision() {
  auto inZAndW = gman::Vector4<T>(0, 0, 1, 1);
  auto inXYAndZ = gman::Vector4<T>(1, 1, 1, 0);
  expectClose(label<T>("magnitude of (0, 0, 1, 1)"), inZAndW.magnitude(), std::numbers::sqrt2_v<T>, 2);
  expectClose(label<T>("magnitude of (1, 1, 1, 0)"), inXYAndZ.magnitude(), std::numbers::sqrt3_v<T>, 2);
}

template <typename T> void testNormalize() {
  auto v = gman::Vector4<T>(1, 2, 2, 4);
  v.normalize();
  expectClose(label<T>("normalize"), v, {T(1) / 5, T(2) / 5, T(2) / 5, T(4) / 5}, 2);
}

template <typename T> void testNormalizeReturnsSelf() {
  auto v = gman::Vector4<T>(1, 2, 2, 4);
  auto& result = v.normalize();
  expectBool(label<T>("normalize returns *this"), &result == &v, true);
}

template <typename T> void testNormalizeRoundsAtItsOwnPrecision() {
  auto v = gman::Vector4<T>(1, 1, 1, 0);
  v.normalize();
  constexpr T unit = std::numbers::inv_sqrt3_v<T>;
  expectClose(label<T>("normalize of (1, 1, 1, 0)"), v, {unit, unit, unit, 0}, 4);
}

template <typename T> void testNormalizeLeavesTheZeroVector() {
  auto v = gman::Vector4<T>(0, 0, 0, 0);
  v.normalize();
  expectExact(label<T>("normalize of the zero vector"), v, {0, 0, 0, 0});
}

template <typename T> void testDot() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  expectExact(label<T>("dot"), v.dot(gman::Vector4<T>(5, 6, 7, 8)), 70);
}

template <typename T> void testDotRoundsAtItsOwnPrecision() {
  constexpr T ax = T(1) / 3;
  constexpr T ay = T(1) / 7;
  constexpr T az = T(1) / 11;
  constexpr T aw = T(1) / 19;
  constexpr T bx = T(1) / 5;
  constexpr T by = T(1) / 13;
  constexpr T bz = T(1) / 17;
  constexpr T bw = T(1) / 23;
  constexpr T expected = ax * bx + ay * by + az * bz + aw * bw;
  auto a = gman::Vector4<T>(ax, ay, az, aw);
  expectClose(label<T>("dot of non-dyadic components"), a.dot(gman::Vector4<T>(bx, by, bz, bw)), expected, 2);
}

template <typename T> void testMatrixMultiplyAssign() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  auto& result = (v *= sequentialMatrix<T>());
  expectExact(label<T>("operator*=(Matrix4) applies the matrix to a row"), v, {90, 100, 110, 120});
  expectBool(label<T>("operator*=(Matrix4) returns *this"), &result == &v, true);
}

template <typename T> void testMatrixMultiplyAssignTranslates() {
  auto v = gman::Vector4<T>(1, 2, 3, 1);
  auto translation = gman::Matrix4<T>();
  translation.trans(10, 20, 30);
  v *= translation;
  expectExact(label<T>("operator*=(Matrix4) by a translation"), v, {11, 22, 33, 1});
}

template <typename T> void testConvertToHPoint() {
  auto v = gman::Vector4<T>(1, 2, 3, 4);
  const auto h = static_cast<gman::HPoint<T>>(v);
  expectExact(label<T>("operator HPoint<T>()"), h, {1, 2, 3, 4});
}

template <typename T> void testProjTransformPoint() {
  if constexpr (std::is_same_v<T, RtFloat>) {
    const auto matrix = sequentialMatrix<T>();
    auto v = gman::Vector4<T>();
    v.projTransform(gman::Point<T>(1, 2, 3), matrix.get());
    expectExact(label<T>("projTransform(Point, RtMatrix)"), v, {18, 46, 74, 102});
  }
}

template <typename T> void testProjTransformVector() {
  if constexpr (std::is_same_v<T, RtFloat>) {
    const auto matrix = sequentialMatrix<T>();
    auto v = gman::Vector4<T>();
    v.projTransform(gman::Vector<T>(1, 2, 3), matrix.get());
    expectExact(label<T>("projTransform(Vector, RtMatrix)"), v, {18, 46, 74, 102});
  }
}

template <typename T> void testPerspectivePoint() {
  auto v = gman::Vector4<T>(2, 4, 6, 2);
  auto p = gman::Point<T>(9, 9, 9);
  v.perspective(p);
  expectExact(label<T>("perspective(Point&) divides by w"), p, {1, 2, 3});
}

template <typename T> void testPerspectiveVector() {
  const auto v = gman::Vector4<T>(2, 4, 6, 2);
  auto divided = gman::Vector<T>(9, 9, 9);
  v.perspective(divided);
  expectExact(label<T>("perspective(Vector&) divides by w"), divided, {1, 2, 3});
}

template <typename T> void testPerspectiveRoundsAtItsOwnPrecision() {
  const auto v = gman::Vector4<T>(1, 2, 3, 3);
  auto divided = gman::Vector<T>(9, 9, 9);
  v.perspective(divided);
  expectClose(label<T>("perspective(Vector&) by 3"), divided, {T(1) / 3, T(2) / 3, 1}, 1);
}

template <typename T> void runAll() {
  testDefaultConstructorZeroesTheVectorPart<T>();
  testComponentConstructor<T>();
  testPointerConstructor<T>();
  testHPointConstructor<T>();
  testConvertingConstructor<T>();
  testCopyConstruct<T>();
  testCopyAssign<T>();
  testMoveConstruct<T>();
  testMoveAssign<T>();
  testGetters<T>();
  testSetters<T>();
  testIndexRead<T>();
  testIndexWrite<T>();
  testAssignScalar<T>();
  testAssignVectorSetsWToOne<T>();
  testAddition<T>();
  testSubtraction<T>();
  testSubtractAssign<T>();
  testMultiplyAssign<T>();
  testMultiplyAssignScalar<T>();
  testDivideAssignScalar<T>();
  testMultiplyScalar<T>();
  testMultiplyScalarLeavesItsOperand<T>();
  testDivideScalar<T>();
  testDivideScalarRoundsAtItsOwnPrecision<T>();
  testMagnitude<T>();
  testMagnitudeRoundsAtItsOwnPrecision<T>();
  testNormalize<T>();
  testNormalizeReturnsSelf<T>();
  testNormalizeRoundsAtItsOwnPrecision<T>();
  testNormalizeLeavesTheZeroVector<T>();
  testDot<T>();
  testDotRoundsAtItsOwnPrecision<T>();
  testMatrixMultiplyAssign<T>();
  testMatrixMultiplyAssignTranslates<T>();
  testConvertToHPoint<T>();
  testProjTransformPoint<T>();
  testProjTransformVector<T>();
  testPerspectivePoint<T>();
  testPerspectiveVector<T>();
  testPerspectiveRoundsAtItsOwnPrecision<T>();
}

} // namespace

int main() {
  runAll<RtFloat>();
  runAll<gman::PartnerReal>();
  return checkSummary("vector4template: all checks passed");
}
