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
 * gman::Vector<T> for RtFloat and gman::PartnerReal, one named check per
 * member and instance: constructors, copies and moves, accessors, indexing,
 * every arithmetic operator, dot, cross, magnitude and normalize. A failure
 * names the member, the instance, the component, and the expected and actual
 * values. Vector::operator< is not tested: its contract awaits a ruling.
 */

#include <numbers>
#include <string>
#include <utility>

#include "check.h"
#include "geometrycheck.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> std::string label(std::string const& name) { return member<T>("Vector", name); }

template <typename T> void testDefaultConstructor() {
  const auto v = gman::Vector<T>();
  expectExact(label<T>("Vector()"), v, {0, 0, 0});
}

template <typename T> void testComponentConstructor() {
  const auto v = gman::Vector<T>(1, 2, 3);
  expectExact(label<T>("Vector(x, y, z)"), v, {1, 2, 3});
}

template <typename T> void testPointerConstructorFromRtFloat() {
  constexpr RtFloat parts[3] = {4.5, 5.25, 6.125};
  const auto v = gman::Vector<T>(parts);
  expectExact(label<T>("Vector(RtFloat const*)"), v, {4.5, 5.25, 6.125});
}

template <typename T> void testPointerConstructorFromPartnerReal() {
  constexpr gman::PartnerReal parts[3] = {4.5, 5.25, 6.125};
  const auto v = gman::Vector<T>(parts);
  expectExact(label<T>("Vector(PartnerReal const*)"), v, {4.5, 5.25, 6.125});
}

template <typename T> void testConvertingConstructor() {
  using U = OtherInstance<T>;
  const auto source = gman::Vector<U>(U(1) / 3, U(2) / 7, U(-5) / 11);
  const auto converted = gman::Vector<T>(source);
  expectExact(label<T>("Vector(Vector<U> const&)"), converted,
              {static_cast<T>(source.getX()), static_cast<T>(source.getY()), static_cast<T>(source.getZ())});
}

template <typename T> void testPointConstructor() {
  const auto v = gman::Vector<T>(gman::Point<T>(1, 2, 3));
  expectExact(label<T>("Vector(Point const&)"), v, {1, 2, 3});
}

template <typename T> void testPointPairConstructor() {
  const auto v = gman::Vector<T>(gman::Point<T>(1, 2, 3), gman::Point<T>(5, 9, 14));
  expectExact(label<T>("Vector(Point a, Point b) is b - a"), v, {4, 7, 11});
}

template <typename T> void testCopyConstruct() {
  const auto source = gman::Vector<T>(1, 2, 3);
  const auto copy = source;
  expectExact(label<T>("Vector(Vector const&)"), copy, {1, 2, 3});
}

template <typename T> void testCopyConstructIsIndependent() {
  const auto source = gman::Vector<T>(1, 2, 3);
  auto copy = source;
  copy.setX(99);
  expectExact(label<T>("Vector(Vector const&) copies, not aliases"), source, {1, 2, 3});
}

template <typename T> void testCopyAssign() {
  const auto source = gman::Vector<T>(1, 2, 3);
  auto target = gman::Vector<T>(9, 9, 9);
  auto& result = (target = source);
  expectExact(label<T>("operator=(Vector const&)"), target, {1, 2, 3});
  expectBool(label<T>("operator=(Vector const&) returns *this"), &result == &target, true);
}

template <typename T> void testMoveConstruct() {
  auto source = gman::Vector<T>(1, 2, 3);
  const auto moved = std::move(source);
  expectExact(label<T>("Vector(Vector&&)"), moved, {1, 2, 3});
}

template <typename T> void testMoveAssign() {
  auto source = gman::Vector<T>(1, 2, 3);
  auto target = gman::Vector<T>(9, 9, 9);
  target = std::move(source);
  expectExact(label<T>("operator=(Vector&&)"), target, {1, 2, 3});
}

template <typename T> void testGetters() {
  const auto v = gman::Vector<T>(1, 2, 3);
  expectExact(label<T>("getX"), v.getX(), 1);
  expectExact(label<T>("getY"), v.getY(), 2);
  expectExact(label<T>("getZ"), v.getZ(), 3);
}

template <typename T> void testSetX() {
  auto v = gman::Vector<T>(1, 2, 3);
  v.setX(10);
  expectExact(label<T>("setX"), v, {10, 2, 3});
}

template <typename T> void testSetY() {
  auto v = gman::Vector<T>(1, 2, 3);
  v.setY(10);
  expectExact(label<T>("setY"), v, {1, 10, 3});
}

template <typename T> void testSetZ() {
  auto v = gman::Vector<T>(1, 2, 3);
  v.setZ(10);
  expectExact(label<T>("setZ"), v, {1, 2, 10});
}

template <typename T> void testIndexRead() {
  auto v = gman::Vector<T>(1, 2, 3);
  expectExact(label<T>("operator[](0)"), v[0], 1);
  expectExact(label<T>("operator[](1)"), v[1], 2);
  expectExact(label<T>("operator[](2)"), v[2], 3);
}

template <typename T> void testIndexReadConst() {
  const auto v = gman::Vector<T>(1, 2, 3);
  expectExact(label<T>("operator[](0) const"), v[0], 1);
  expectExact(label<T>("operator[](1) const"), v[1], 2);
  expectExact(label<T>("operator[](2) const"), v[2], 3);
}

template <typename T> void testIndexWrite() {
  auto atX = gman::Vector<T>(1, 2, 3);
  auto atY = gman::Vector<T>(1, 2, 3);
  auto atZ = gman::Vector<T>(1, 2, 3);
  atX[0] = 10;
  atY[1] = 10;
  atZ[2] = 10;
  expectExact(label<T>("operator[](0) writes x"), atX, {10, 2, 3});
  expectExact(label<T>("operator[](1) writes y"), atY, {1, 10, 3});
  expectExact(label<T>("operator[](2) writes z"), atZ, {1, 2, 10});
}

template <typename T> void testConvertToPoint() {
  auto v = gman::Vector<T>(1, 2, 3);
  const auto p = static_cast<gman::Point<T>>(v);
  expectExact(label<T>("operator Point<T>()"), p, {1, 2, 3});
}

template <typename T> void testAssignPoint() {
  auto v = gman::Vector<T>(9, 9, 9);
  auto& result = (v = gman::Point<T>(1, 2, 3));
  expectExact(label<T>("operator=(Point const&)"), v, {1, 2, 3});
  expectBool(label<T>("operator=(Point const&) returns *this"), &result == &v, true);
}

template <typename T> void testAssignScalar() {
  auto v = gman::Vector<T>(1, 2, 3);
  auto& result = (v = T(7));
  expectExact(label<T>("operator=(T)"), v, {7, 7, 7});
  expectBool(label<T>("operator=(T) returns *this"), &result == &v, true);
}

template <typename T> void testAddition() {
  const auto sum = gman::Vector<T>(1, 2, 3) + gman::Vector<T>(10, 20, 40);
  expectExact(label<T>("operator+(Vector)"), sum, {11, 22, 43});
}

template <typename T> void testSubtraction() {
  const auto difference = gman::Vector<T>(10, 20, 40) - gman::Vector<T>(1, 2, 3);
  expectExact(label<T>("operator-(Vector)"), difference, {9, 18, 37});
}

template <typename T> void testUnaryMinus() {
  const auto negated = -gman::Vector<T>(1, -2, 3);
  expectExact(label<T>("operator-()"), negated, {-1, 2, -3});
}

template <typename T> void testAddScalar() {
  const auto shifted = gman::Vector<T>(1, 2, 3) + T(10);
  expectExact(label<T>("operator+(T)"), shifted, {11, 12, 13});
}

template <typename T> void testSubtractScalar() {
  const auto shifted = gman::Vector<T>(11, 12, 13) - T(10);
  expectExact(label<T>("operator-(T)"), shifted, {1, 2, 3});
}

template <typename T> void testMultiplyScalar() {
  const auto scaled = gman::Vector<T>(1, 2, 3) * T(4);
  expectExact(label<T>("operator*(T)"), scaled, {4, 8, 12});
}

template <typename T> void testDivideScalar() {
  const auto scaled = gman::Vector<T>(1, 2, 3) / T(2);
  expectExact(label<T>("operator/(T)"), scaled, {T(1) / 2, 1, T(3) / 2});
}

template <typename T> void testDivideScalarRoundsAtItsOwnPrecision() {
  const auto scaled = gman::Vector<T>(1, 2, 3) / T(3);
  expectClose(label<T>("operator/(T) by 3"), scaled, {T(1) / 3, T(2) / 3, 1}, 1);
}

template <typename T> void testAddAssign() {
  auto v = gman::Vector<T>(1, 2, 3);
  auto& result = (v += gman::Vector<T>(10, 20, 40));
  expectExact(label<T>("operator+=(Vector)"), v, {11, 22, 43});
  expectBool(label<T>("operator+=(Vector) returns *this"), &result == &v, true);
}

template <typename T> void testSubtractAssign() {
  auto v = gman::Vector<T>(10, 20, 40);
  auto& result = (v -= gman::Vector<T>(1, 2, 3));
  expectExact(label<T>("operator-=(Vector)"), v, {9, 18, 37});
  expectBool(label<T>("operator-=(Vector) returns *this"), &result == &v, true);
}

template <typename T> void testMultiplyAssign() {
  auto v = gman::Vector<T>(2, 4, 8);
  auto& result = (v *= gman::Vector<T>(3, 5, 7));
  expectExact(label<T>("operator*=(Vector) is componentwise"), v, {6, 20, 56});
  expectBool(label<T>("operator*=(Vector) returns *this"), &result == &v, true);
}

template <typename T> void testDivideAssign() {
  auto v = gman::Vector<T>(6, 20, 56);
  auto& result = (v /= gman::Vector<T>(3, 5, 7));
  expectExact(label<T>("operator/=(Vector) is componentwise"), v, {2, 4, 8});
  expectBool(label<T>("operator/=(Vector) returns *this"), &result == &v, true);
}

template <typename T> void testAddAssignScalar() {
  auto v = gman::Vector<T>(1, 2, 3);
  auto& result = (v += T(10));
  expectExact(label<T>("operator+=(T)"), v, {11, 12, 13});
  expectBool(label<T>("operator+=(T) returns *this"), &result == &v, true);
}

template <typename T> void testSubtractAssignScalar() {
  auto v = gman::Vector<T>(11, 12, 13);
  auto& result = (v -= T(10));
  expectExact(label<T>("operator-=(T)"), v, {1, 2, 3});
  expectBool(label<T>("operator-=(T) returns *this"), &result == &v, true);
}

template <typename T> void testMultiplyAssignScalar() {
  auto v = gman::Vector<T>(1, 2, 3);
  auto& result = (v *= T(4));
  expectExact(label<T>("operator*=(T)"), v, {4, 8, 12});
  expectBool(label<T>("operator*=(T) returns *this"), &result == &v, true);
}

template <typename T> void testDivideAssignScalar() {
  auto v = gman::Vector<T>(1, 2, 3);
  auto& result = (v /= T(2));
  expectExact(label<T>("operator/=(T)"), v, {T(1) / 2, 1, T(3) / 2});
  expectBool(label<T>("operator/=(T) returns *this"), &result == &v, true);
}

template <typename T> void testMatrixMultiplyAssign() {
  auto v = gman::Vector<T>(1, 2, 3);
  auto& result = (v *= sequentialMatrix<T>());
  expectExact(label<T>("operator*=(Matrix4) applies the matrix to a column"), v, {18, 46, 74});
  expectBool(label<T>("operator*=(Matrix4) returns *this"), &result == &v, true);
}

template <typename T> void testDot() {
  const auto d = gman::Vector<T>(1, 2, 3).dot(gman::Vector<T>(4, 5, 6));
  expectExact(label<T>("dot"), d, 32);
}

template <typename T> void testDotRoundsAtItsOwnPrecision() {
  constexpr T ax = T(1) / 3;
  constexpr T ay = T(1) / 7;
  constexpr T az = T(1) / 11;
  constexpr T bx = T(1) / 5;
  constexpr T by = T(1) / 13;
  constexpr T bz = T(1) / 17;
  constexpr T expected = ax * bx + ay * by + az * bz;
  const auto d = gman::Vector<T>(ax, ay, az).dot(gman::Vector<T>(bx, by, bz));
  expectClose(label<T>("dot of non-dyadic components"), d, expected, 2);
}

template <typename T> void testCross() {
  const auto c = gman::Vector<T>(1, 2, 3).cross(gman::Vector<T>(4, 5, 6));
  expectExact(label<T>("cross"), c, {-3, 6, -3});
}

template <typename T> void testCrossFollowsTheRightHandRule() {
  const auto c = gman::Vector<T>(1, 0, 0).cross(gman::Vector<T>(0, 1, 0));
  expectExact(label<T>("cross of x and y is z"), c, {0, 0, 1});
}

template <typename T> void testMagnitude() {
  auto v = gman::Vector<T>(3, 4, 12);
  expectExact(label<T>("magnitude"), v.magnitude(), 13);
}

template <typename T> void testMagnitudeRoundsAtItsOwnPrecision() {
  auto v = gman::Vector<T>(1, 1, 1);
  expectClose(label<T>("magnitude of (1, 1, 1)"), v.magnitude(), std::numbers::sqrt3_v<T>, 2);
}

template <typename T> void testNormalize() {
  auto v = gman::Vector<T>(0, 3, 4);
  v.normalize();
  expectClose(label<T>("normalize"), v, {0, T(3) / 5, T(4) / 5}, 2);
}

template <typename T> void testNormalizeReturnsSelf() {
  auto v = gman::Vector<T>(0, 3, 4);
  auto& result = v.normalize();
  expectBool(label<T>("normalize returns *this"), &result == &v, true);
}

template <typename T> void testNormalizeRoundsAtItsOwnPrecision() {
  auto v = gman::Vector<T>(1, 1, 1);
  v.normalize();
  constexpr T unit = std::numbers::inv_sqrt3_v<T>;
  expectClose(label<T>("normalize of (1, 1, 1)"), v, {unit, unit, unit}, 4);
}

template <typename T> void testNormalizeLeavesTheZeroVector() {
  auto v = gman::Vector<T>(0, 0, 0);
  v.normalize();
  expectExact(label<T>("normalize of the zero vector"), v, {0, 0, 0});
}

template <typename T> void testNormalizeLeavesATinyVector() {
  auto v = gman::Vector<T>(T(1e-11), 0, 0);
  v.normalize();
  expectExact(label<T>("normalize below RI_EPSILON"), v, {T(1e-11), 0, 0});
}

template <typename T> gman::Vector<T> firstWitness() {
  return gman::Vector<T>(reciprocal<T, 3>, reciprocal<T, 7>, reciprocal<T, 11>);
}

template <typename T> gman::Vector<T> secondWitness() {
  return gman::Vector<T>(reciprocal<T, 5>, reciprocal<T, 13>, reciprocal<T, 17>);
}

template <typename T> void testAdditionRoundsAtItsOwnPrecision() {
  expectClose(label<T>("operator+(Vector) of non-dyadic components"), firstWitness<T>() + secondWitness<T>(),
              {T(8) / 15, T(20) / 91, T(28) / 187}, 2);
}

template <typename T> void testSubtractionRoundsAtItsOwnPrecision() {
  expectClose(label<T>("operator-(Vector) of non-dyadic components"), firstWitness<T>() - secondWitness<T>(),
              {T(2) / 15, T(6) / 91, T(6) / 187}, 2);
}

template <typename T> void testAddScalarRoundsAtItsOwnPrecision() {
  expectClose(label<T>("operator+(T) of non-dyadic components"), firstWitness<T>() + reciprocal<T, 19>,
              {T(22) / 57, T(26) / 133, T(30) / 209}, 2);
}

template <typename T> void testSubtractScalarRoundsAtItsOwnPrecision() {
  expectClose(label<T>("operator-(T) of non-dyadic components"), firstWitness<T>() - reciprocal<T, 19>,
              {T(16) / 57, T(12) / 133, T(8) / 209}, 2);
}

template <typename T> void testMultiplyScalarRoundsAtItsOwnPrecision() {
  expectClose(label<T>("operator*(T) of non-dyadic components"), firstWitness<T>() * reciprocal<T, 19>,
              {T(1) / 57, T(1) / 133, T(1) / 209}, 2);
}

template <typename T> void testAddAssignRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v += secondWitness<T>();
  expectClose(label<T>("operator+=(Vector) of non-dyadic components"), v, {T(8) / 15, T(20) / 91, T(28) / 187}, 2);
}

template <typename T> void testSubtractAssignRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v -= secondWitness<T>();
  expectClose(label<T>("operator-=(Vector) of non-dyadic components"), v, {T(2) / 15, T(6) / 91, T(6) / 187}, 2);
}

template <typename T> void testMultiplyAssignRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v *= secondWitness<T>();
  expectClose(label<T>("operator*=(Vector) of non-dyadic components"), v, {T(1) / 15, T(1) / 91, T(1) / 187}, 2);
}

template <typename T> void testDivideAssignRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v /= secondWitness<T>();
  expectClose(label<T>("operator/=(Vector) of non-dyadic components"), v, {T(5) / 3, T(13) / 7, T(17) / 11}, 4);
}

template <typename T> void testAddAssignScalarRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v += reciprocal<T, 19>;
  expectClose(label<T>("operator+=(T) of non-dyadic components"), v, {T(22) / 57, T(26) / 133, T(30) / 209}, 2);
}

template <typename T> void testSubtractAssignScalarRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v -= reciprocal<T, 19>;
  expectClose(label<T>("operator-=(T) of non-dyadic components"), v, {T(16) / 57, T(12) / 133, T(8) / 209}, 2);
}

template <typename T> void testMultiplyAssignScalarRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v *= reciprocal<T, 19>;
  expectClose(label<T>("operator*=(T) of non-dyadic components"), v, {T(1) / 57, T(1) / 133, T(1) / 209}, 2);
}

template <typename T> void testDivideAssignScalarRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v /= reciprocal<T, 19>;
  expectClose(label<T>("operator/=(T) of non-dyadic components"), v, {T(19) / 3, T(19) / 7, T(19) / 11}, 4);
}

template <typename T> void testMatrixMultiplyAssignRoundsAtItsOwnPrecision() {
  auto v = firstWitness<T>();
  v *= columnWitnessMatrix<T>();
  expectClose(label<T>("operator*=(Matrix4) of non-dyadic entries"), v, {T(14) / 45, T(62) / 637, T(138) / 2057}, 4);
}

template <typename T> void testCrossRoundsAtItsOwnPrecision() {
  expectClose(label<T>("cross of non-dyadic components"), firstWitness<T>().cross(secondWitness<T>()),
              {T(24) / 17017, -T(4) / 2805, -T(4) / 1365}, 2);
}

template <typename T> void runAll() {
  testDefaultConstructor<T>();
  testComponentConstructor<T>();
  testPointerConstructorFromRtFloat<T>();
  testPointerConstructorFromPartnerReal<T>();
  testConvertingConstructor<T>();
  testPointConstructor<T>();
  testPointPairConstructor<T>();
  testCopyConstruct<T>();
  testCopyConstructIsIndependent<T>();
  testCopyAssign<T>();
  testMoveConstruct<T>();
  testMoveAssign<T>();
  testGetters<T>();
  testSetX<T>();
  testSetY<T>();
  testSetZ<T>();
  testIndexRead<T>();
  testIndexReadConst<T>();
  testIndexWrite<T>();
  testConvertToPoint<T>();
  testAssignPoint<T>();
  testAssignScalar<T>();
  testAddition<T>();
  testSubtraction<T>();
  testUnaryMinus<T>();
  testAddScalar<T>();
  testSubtractScalar<T>();
  testMultiplyScalar<T>();
  testDivideScalar<T>();
  testDivideScalarRoundsAtItsOwnPrecision<T>();
  testAddAssign<T>();
  testSubtractAssign<T>();
  testMultiplyAssign<T>();
  testDivideAssign<T>();
  testAddAssignScalar<T>();
  testSubtractAssignScalar<T>();
  testMultiplyAssignScalar<T>();
  testDivideAssignScalar<T>();
  testMatrixMultiplyAssign<T>();
  testDot<T>();
  testDotRoundsAtItsOwnPrecision<T>();
  testCross<T>();
  testCrossFollowsTheRightHandRule<T>();
  testMagnitude<T>();
  testMagnitudeRoundsAtItsOwnPrecision<T>();
  testNormalize<T>();
  testNormalizeReturnsSelf<T>();
  testNormalizeRoundsAtItsOwnPrecision<T>();
  testNormalizeLeavesTheZeroVector<T>();
  testNormalizeLeavesATinyVector<T>();
  testAdditionRoundsAtItsOwnPrecision<T>();
  testSubtractionRoundsAtItsOwnPrecision<T>();
  testAddScalarRoundsAtItsOwnPrecision<T>();
  testSubtractScalarRoundsAtItsOwnPrecision<T>();
  testMultiplyScalarRoundsAtItsOwnPrecision<T>();
  testAddAssignRoundsAtItsOwnPrecision<T>();
  testSubtractAssignRoundsAtItsOwnPrecision<T>();
  testMultiplyAssignRoundsAtItsOwnPrecision<T>();
  testDivideAssignRoundsAtItsOwnPrecision<T>();
  testAddAssignScalarRoundsAtItsOwnPrecision<T>();
  testSubtractAssignScalarRoundsAtItsOwnPrecision<T>();
  testMultiplyAssignScalarRoundsAtItsOwnPrecision<T>();
  testDivideAssignScalarRoundsAtItsOwnPrecision<T>();
  testMatrixMultiplyAssignRoundsAtItsOwnPrecision<T>();
  testCrossRoundsAtItsOwnPrecision<T>();
}

} // namespace

int main() {
  runAll<RtFloat>();
  runAll<gman::PartnerReal>();
  return checkSummary("vectortemplate: all checks passed");
}
