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
 * gman::Matrix4<T> for RtFloat and gman::PartnerReal, one named check per
 * member and instance: constructors, copies and moves, identity, concat,
 * the transform builders, determinant and invert, p3m and p4m, the scalar
 * and matrix operators, setBasis, assign, indexing and get(). A failure
 * names the member, the instance, the matrix entry, and the expected and
 * actual values. Matrix4::operator=(U const (&)[4][4]) is declared and never
 * defined, so no test can call it. gman::transformPoint and
 * gman::transformDirection, which exist for RtFloat alone, are checked at
 * the end.
 */

#include <cstddef>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "geometrycheck.h"
#include "gmanerror.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> std::string label(std::string const& name) { return member<T>("Matrix4", name); }

template <typename T> std::vector<T> identityEntries() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

template <typename T> gman::Matrix4<T> matrixOf(std::vector<T> const& entries) {
  gman::Matrix4<T> m;
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      m[i][j] = entries[4 * i + j];
    }
  }
  return m;
}

template <typename T> gman::Matrix4<T> denseMatrix() {
  return matrixOf<T>({2, 0, 1, 0, 1, 3, 0, 1, 0, 1, 4, 2, 1, 0, 2, 5});
}

template <typename T> gman::Matrix4<T> evenDeterminantMatrix() {
  return matrixOf<T>({1, 2, 2, 0, 4, 3, 4, 1, 1, 3, 3, 0, 4, 1, 3, 0});
}

template <typename T> gman::Matrix4<T> oddDeterminantMatrix() {
  return matrixOf<T>({2, 3, 3, 3, 1, 1, 0, 0, 2, 2, 2, 3, 2, 3, 4, 1});
}

template <typename T> void testDefaultConstructorIsTheIdentity() {
  expectExact(label<T>("Matrix4() is the identity"), gman::Matrix4<T>(), identityEntries<T>());
}

template <typename T> void testArrayConstructorFromRtFloat() {
  const RtFloat source[4][4] = {{0.5, 1, 1.5, 2}, {2.5, 3, 3.5, 4}, {4.5, 5, 5.5, 6}, {6.5, 7, 7.5, 8}};
  const auto m = gman::Matrix4<T>(source);
  expectExact(label<T>("Matrix4(RtFloat const (*)[4])"), m,
              {0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8});
}

template <typename T> void testArrayConstructorFromPartnerReal() {
  const gman::PartnerReal source[4][4] = {{0.5, 1, 1.5, 2}, {2.5, 3, 3.5, 4}, {4.5, 5, 5.5, 6}, {6.5, 7, 7.5, 8}};
  const auto m = gman::Matrix4<T>(source);
  expectExact(label<T>("Matrix4(PartnerReal const (*)[4])"), m,
              {0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8});
}

template <typename T> void testConvertingConstructor() {
  using U = OtherInstance<T>;
  auto source = gman::Matrix4<U>();
  std::vector<T> expected;
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      source[i][j] = static_cast<U>(4 * i + j + 1) / 3;
      expected.push_back(static_cast<T>(source[i][j]));
    }
  }
  expectExact(label<T>("Matrix4(Matrix4<U> const&)"), gman::Matrix4<T>(source), expected);
}

template <typename T> void testCopyConstruct() {
  const auto source = sequentialMatrix<T>();
  const auto copy = source;
  expectExact(label<T>("Matrix4(Matrix4 const&)"), copy, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
}

template <typename T> void testCopyConstructIsIndependent() {
  const auto source = sequentialMatrix<T>();
  auto copy = source;
  copy[0][0] = 99;
  expectExact(label<T>("Matrix4(Matrix4 const&) copies, not aliases"), source,
              {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
}

template <typename T> void testCopyAssign() {
  const auto source = sequentialMatrix<T>();
  auto target = gman::Matrix4<T>();
  auto& result = (target = source);
  expectExact(label<T>("operator=(Matrix4 const&)"), target, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
  expectBool(label<T>("operator=(Matrix4 const&) returns *this"), &result == &target, true);
}

template <typename T> void testMoveConstruct() {
  auto source = sequentialMatrix<T>();
  const auto moved = std::move(source);
  expectExact(label<T>("Matrix4(Matrix4&&)"), moved, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
}

template <typename T> void testMoveAssign() {
  auto source = sequentialMatrix<T>();
  auto target = gman::Matrix4<T>();
  target = std::move(source);
  expectExact(label<T>("operator=(Matrix4&&)"), target, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
}

template <typename T> void testIdentity() {
  auto m = sequentialMatrix<T>();
  m.identity();
  expectExact(label<T>("identity"), m, identityEntries<T>());
}

template <typename T> void testConcatAppliesTheArgumentAfterTheReceiver() {
  auto translation = gman::Matrix4<T>();
  translation.trans(1, 2, 3);
  auto scaling = gman::Matrix4<T>();
  scaling.scale(2, 3, 4);
  translation.concat(scaling);
  expectExact(label<T>("concat of a translation and a scale"), translation,
              {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 2, 6, 12, 1});
}

template <typename T> void testConcatTheOtherWay() {
  auto translation = gman::Matrix4<T>();
  translation.trans(1, 2, 3);
  auto scaling = gman::Matrix4<T>();
  scaling.scale(2, 3, 4);
  scaling.concat(translation);
  expectExact(label<T>("concat of a scale and a translation"), scaling,
              {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 2, 3, 1});
}

template <typename T> void testPersp() {
  auto m = gman::Matrix4<T>();
  m.persp(90);
  expectClose(label<T>("persp of 90 degrees"), m, {1, 0, 0, 1, 0, 1, 0, -1, 0, 0, 1, 1, 0, 0, 0, 0}, 2);
}

template <typename T> void testPerspRoundsAtItsOwnPrecision() {
  auto m = gman::Matrix4<T>();
  m.persp(60);
  constexpr T t = std::numbers::inv_sqrt3_v<T>;
  expectClose(label<T>("persp of 60 degrees"), m, {1, 0, 0, t, 0, 1, 0, -t, 0, 0, 1, t, 0, 0, 0, 0}, 2);
}

template <typename T> void testTrans() {
  auto m = gman::Matrix4<T>();
  m.trans(1, 2, 3);
  expectExact(label<T>("trans"), m, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1});
}

template <typename T> void testRotQuarterTurnAboutZ() {
  auto m = gman::Matrix4<T>();
  m.rot(std::numbers::pi_v<T> / 2, 0, 0, 1);
  expectClose(label<T>("rot a quarter turn about z"), m, {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, 4);
}

template <typename T> void testRotNormalizesItsAxis() {
  auto m = gman::Matrix4<T>();
  m.rot(std::numbers::pi_v<T> / 2, 0, 0, 2);
  expectClose(label<T>("rot about an axis of length 2"), m, {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, 4);
}

template <typename T> void testRotThirdTurnAboutTheDiagonal() {
  auto m = gman::Matrix4<T>();
  m.rot(T(2) * std::numbers::pi_v<T> / 3, 1, 1, 1);
  expectClose(label<T>("rot a third turn about (1, 1, 1)"), m, {0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1}, 8);
}

template <typename T> void testScale() {
  auto m = gman::Matrix4<T>();
  m.scale(2, 3, 4);
  expectExact(label<T>("scale"), m, {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 1});
}

template <typename T> void testSkew() {
  auto a = gman::Vector<T>(0, 1, 0);
  auto b = gman::Vector<T>(1, 0, 0);
  auto m = gman::Matrix4<T>();
  m.skew(45, a, b);
  expectClose(label<T>("skew of 45 degrees"), m, {1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, 4);
}

template <typename T> void testSkewRoundsAtItsOwnPrecision() {
  auto a = gman::Vector<T>(0, 1, 0);
  auto b = gman::Vector<T>(1, 0, 0);
  auto m = gman::Matrix4<T>();
  m.skew(30, a, b);
  constexpr T t = std::numbers::inv_sqrt3_v<T>;
  expectClose(label<T>("skew of 30 degrees"), m, {1, 0, 0, 0, t, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, 8);
}

template <typename T> void testSkewTooLargeThrows() {
  auto a = gman::Vector<T>(0, 1, 0);
  auto b = gman::Vector<T>(1, 0, 0);
  auto m = gman::Matrix4<T>();
  expectThrows<GMANError>(label<T>("skew of 135 degrees"), [&] { m.skew(135, a, b); });
}

template <typename T> void testPrjPersp() {
  auto m = gman::Matrix4<T>();
  m.prjPersp(90, 1, 3);
  expectClose(label<T>("prjPersp of 90 degrees from 1 to 3"), m, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, -3, 0, 0, 1, 0}, 2);
}

template <typename T> void testPrjPerspRoundsAtItsOwnPrecision() {
  auto m = gman::Matrix4<T>();
  m.prjPersp(60, 1, 4);
  constexpr T s = std::numbers::sqrt3_v<T>;
  expectClose(label<T>("prjPersp of 60 degrees from 1 to 4"), m,
              {s, 0, 0, 0, 0, s, 0, 0, 0, 0, T(5) / 3, -T(8) / 3, 0, 0, 1, 0}, 4);
}

template <typename T> void testPrjOrtho() {
  auto m = gman::Matrix4<T>();
  m.prjOrtho(1, 5);
  expectExact(label<T>("prjOrtho from 1 to 5"), m, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.5, -1.5, 0, 0, 0, 1});
}

template <typename T> void testPrjOrthoRoundsAtItsOwnPrecision() {
  auto m = gman::Matrix4<T>();
  m.prjOrtho(1, 4);
  expectClose(label<T>("prjOrtho from 1 to 4"), m, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, T(2) / 3, -T(5) / 3, 0, 0, 0, 1}, 2);
}

template <typename T> void testDeterminantOfTheIdentity() {
  auto m = gman::Matrix4<T>();
  expectExact(label<T>("determinant of the identity"), m.determinant(), 1);
}

template <typename T> void testDeterminantOfAScale() {
  auto m = gman::Matrix4<T>();
  m.scale(2, 3, 4);
  expectExact(label<T>("determinant of a scale"), m.determinant(), 24);
}

template <typename T> void testDeterminantOfADenseMatrix() {
  auto m = denseMatrix<T>();
  expectExact(label<T>("determinant of a dense matrix"), m.determinant(), 110);
}

template <typename T> void testDeterminantOfASingularMatrix() {
  auto m = sequentialMatrix<T>();
  expectExact(label<T>("determinant of a singular matrix"), m.determinant(), 0);
}

template <typename T> void testInvertAScale() {
  auto m = gman::Matrix4<T>();
  m.scale(2, 4, 8);
  m.invert();
  expectExact(label<T>("invert of a scale"), m, {0.5, 0, 0, 0, 0, 0.25, 0, 0, 0, 0, 0.125, 0, 0, 0, 0, 1});
}

template <typename T> void testInvertWithAPowerOfTwoDeterminant() {
  auto m = evenDeterminantMatrix<T>();
  m.invert();
  expectExact(label<T>("invert of a matrix with determinant 2"), m,
              {3, 0, -2, 0, 4.5, 0, -2.5, -0.5, -5.5, 0, 3.5, 0.5, -3.5, 1, 1.5, -0.5});
}

template <typename T> void testInvertRoundsAtItsOwnPrecision() {
  auto m = oddDeterminantMatrix<T>();
  m.invert();
  constexpr T numerators[16] = {-10, 3, 9, 3, 10, 4, -9, -3, -3, -4, 2, 3, 2, -2, 1, -2};
  std::vector<T> expected;
  for (const auto numerator : numerators) {
    expected.push_back(numerator / 7);
  }
  expectClose(label<T>("invert of a matrix with determinant 7"), m, expected, 4);
}

template <typename T> void testInvertASingularMatrixThrows() {
  auto m = sequentialMatrix<T>();
  expectThrows<GMANError>(label<T>("invert of a singular matrix"), [&] { m.invert(); });
}

template <typename T> void testMultiplyScalar() {
  const auto scaled = sequentialMatrix<T>() * T(2);
  expectExact(label<T>("operator*(T)"), scaled, {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32});
}

template <typename T> void testMultiplyScalarLeavesItsOperand() {
  const auto m = sequentialMatrix<T>();
  static_cast<void>(m * T(2));
  expectExact(label<T>("operator*(T) leaves its operand"), m, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
}

template <typename T> void testMultiplyAssignScalar() {
  auto m = sequentialMatrix<T>();
  auto& result = (m *= T(2));
  expectExact(label<T>("operator*=(T)"), m, {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32});
  expectBool(label<T>("operator*=(T) returns *this"), &result == &m, true);
}

template <typename T> void testMultiplyMatrix() {
  const auto product = sequentialMatrix<T>() * denseMatrix<T>();
  expectExact(label<T>("operator*(Matrix4)"), product,
              {8, 9, 21, 28, 24, 25, 49, 60, 40, 41, 77, 92, 56, 57, 105, 124});
}

template <typename T> void testMultiplyMatrixTheOtherWay() {
  const auto product = denseMatrix<T>() * sequentialMatrix<T>();
  expectExact(label<T>("operator*(Matrix4) in the other order"), product,
              {11, 14, 17, 20, 29, 34, 39, 44, 67, 74, 81, 88, 84, 92, 100, 108});
}

template <typename T> void testMultiplyMatrixLeavesItsOperands() {
  const auto a = sequentialMatrix<T>();
  const auto b = denseMatrix<T>();
  static_cast<void>(a * b);
  expectExact(label<T>("operator*(Matrix4) leaves the left operand"), a,
              {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
  expectExact(label<T>("operator*(Matrix4) leaves the right operand"), b,
              {2, 0, 1, 0, 1, 3, 0, 1, 0, 1, 4, 2, 1, 0, 2, 5});
}

template <typename T> void testMultiplyAssignMatrix() {
  auto m = sequentialMatrix<T>();
  auto& result = (m *= denseMatrix<T>());
  expectExact(label<T>("operator*=(Matrix4)"), m, {8, 9, 21, 28, 24, 25, 49, 60, 40, 41, 77, 92, 56, 57, 105, 124});
  expectBool(label<T>("operator*=(Matrix4) returns *this"), &result == &m, true);
}

template <typename T> void testMultiplyAssignSelf() {
  auto m = sequentialMatrix<T>();
  m *= m;
  expectExact(label<T>("operator*=(Matrix4) by itself"), m,
              {90, 100, 110, 120, 202, 228, 254, 280, 314, 356, 398, 440, 426, 484, 542, 600});
}

template <typename T> void testAddition() {
  const auto sum = sequentialMatrix<T>() + denseMatrix<T>();
  expectExact(label<T>("operator+(Matrix4)"), sum, {3, 2, 4, 4, 6, 9, 7, 9, 9, 11, 15, 14, 14, 14, 17, 21});
}

template <typename T> void testAddAssign() {
  auto m = sequentialMatrix<T>();
  auto& result = (m += denseMatrix<T>());
  expectExact(label<T>("operator+=(Matrix4)"), m, {3, 2, 4, 4, 6, 9, 7, 9, 9, 11, 15, 14, 14, 14, 17, 21});
  expectBool(label<T>("operator+=(Matrix4) returns *this"), &result == &m, true);
}

template <typename T> void testAssign() {
  auto target = gman::Matrix4<T>();
  auto& result = target.assign(sequentialMatrix<T>());
  expectExact(label<T>("assign"), target, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
  expectBool(label<T>("assign returns *this"), &result == &target, true);
}

template <typename T> void testSetBasisFromRtFloat() {
  const RtFloat basis[4][4] = {{-1, 3, -3, 1}, {3, -6, 3, 0}, {-3, 3, 0, 0}, {1, 0, 0, 0}};
  auto m = gman::Matrix4<T>();
  m.setBasis(basis);
  expectExact(label<T>("setBasis(RtFloat const (&)[4][4])"), m, {-1, 3, -3, 1, 3, -6, 3, 0, -3, 3, 0, 0, 1, 0, 0, 0});
}

template <typename T> void testSetBasisFromPartnerReal() {
  const gman::PartnerReal basis[4][4] = {{-1, 3, -3, 1}, {3, -6, 3, 0}, {-3, 3, 0, 0}, {1, 0, 0, 0}};
  auto m = gman::Matrix4<T>();
  m.setBasis(basis);
  expectExact(label<T>("setBasis(PartnerReal const (&)[4][4])"), m,
              {-1, 3, -3, 1, 3, -6, 3, 0, -3, 3, 0, 0, 1, 0, 0, 0});
}

template <typename T> void testIndexRead() {
  auto m = sequentialMatrix<T>();
  expectExact(label<T>("operator[](0)[0]"), m[0][0], 1);
  expectExact(label<T>("operator[](1)[2]"), m[1][2], 7);
  expectExact(label<T>("operator[](3)[1]"), m[3][1], 14);
}

template <typename T> void testIndexReadConst() {
  const auto m = sequentialMatrix<T>();
  expectExact(label<T>("operator[](0)[0] const"), m[0][0], 1);
  expectExact(label<T>("operator[](1)[2] const"), m[1][2], 7);
  expectExact(label<T>("operator[](3)[1] const"), m[3][1], 14);
}

template <typename T> void testIndexWrite() {
  auto m = gman::Matrix4<T>();
  m[2][1] = 99;
  expectExact(label<T>("operator[](2)[1] writes row 2, column 1"), m,
              {1, 0, 0, 0, 0, 1, 0, 0, 0, 99, 1, 0, 0, 0, 0, 1});
}

template <typename T> void testGet() {
  const auto m = sequentialMatrix<T>();
  expectExact(label<T>("get"), m.get(), {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
}

template <typename T> void testGetRefersToTheMatrixStorage() {
  const auto m = sequentialMatrix<T>();
  expectBool(label<T>("get refers to the matrix's own storage"), &m.get()[0][0] == &m[0][0], true);
}

template <typename T> void testP3mTranslates() {
  auto m = gman::Matrix4<T>();
  m.trans(10, 20, 30);
  T source[3] = {1, 2, 3};
  T destination[3];
  m.p3m(1, source, destination);
  expectExact(label<T>("p3m by a translation"), std::vector<T>(destination, destination + 3), {11, 22, 33});
}

template <typename T> void testP3mTransformsEachPoint() {
  const auto m = matrixOf<T>({1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 10, 11, 12, 1});
  T source[6] = {1, 2, 3, 4, 5, 6};
  T destination[6];
  m.p3m(2, source, destination);
  expectExact(label<T>("p3m of two points"), std::vector<T>(destination, destination + 6), {40, 47, 54, 76, 92, 108});
}

template <typename T> void testP3mDividesByW() {
  auto m = gman::Matrix4<T>();
  m[3][3] = 2;
  T source[3] = {2, 4, 6};
  T destination[3];
  m.p3m(1, source, destination);
  expectExact(label<T>("p3m divides by w of 2"), std::vector<T>(destination, destination + 3), {1, 2, 3});
}

template <typename T> void testP3mDividesByWRoundsAtItsOwnPrecision() {
  auto m = gman::Matrix4<T>();
  m[3][3] = 3;
  T source[3] = {1, 2, 3};
  T destination[3];
  m.p3m(1, source, destination);
  expectClose(label<T>("p3m divides by w of 3"), std::vector<T>(destination, destination + 3), {T(1) / 3, T(2) / 3, 1},
              2);
}

template <typename T> void testP3mLeavesAPointWhoseWIsZero() {
  auto m = gman::Matrix4<T>();
  m[3][3] = 0;
  T source[3] = {1, 2, 3};
  T destination[3];
  m.p3m(1, source, destination);
  expectExact(label<T>("p3m with w of 0 does not divide"), std::vector<T>(destination, destination + 3), {1, 2, 3});
}

template <typename T> void testP4m() {
  const auto m = sequentialMatrix<T>();
  T source[4] = {1, 2, 3, 4};
  T destination[4];
  m.p4m(1, source, destination);
  expectExact(label<T>("p4m"), std::vector<T>(destination, destination + 4), {90, 100, 110, 120});
}

template <typename T> void testP4mTransformsEachPoint() {
  const auto m = sequentialMatrix<T>();
  T source[8] = {1, 2, 3, 4, 0, 1, 0, 0};
  T destination[8];
  m.p4m(2, source, destination);
  expectExact(label<T>("p4m of two points"), std::vector<T>(destination, destination + 8),
              {90, 100, 110, 120, 5, 6, 7, 8});
}

template <typename T> void testP4mKeepsW() {
  auto m = gman::Matrix4<T>();
  m[3][3] = 2;
  T source[4] = {1, 2, 3, 4};
  T destination[4];
  m.p4m(1, source, destination);
  expectExact(label<T>("p4m does not divide by w"), std::vector<T>(destination, destination + 4), {1, 2, 3, 8});
}

void testTransformPoint() {
  auto m = gman::Matrix4<RtFloat>();
  m.scale(2, 3, 4);
  m.trans(10, 20, 30);
  const auto p = gman::transformPoint(m, gman::Point<RtFloat>(1, 2, 3));
  expectExact("gman::transformPoint", p, {12, 26, 42});
}

void testTransformDirection() {
  auto m = gman::Matrix4<RtFloat>();
  m.scale(2, 3, 4);
  m.trans(10, 20, 30);
  const auto v = gman::transformDirection(m, gman::Vector<RtFloat>(1, 2, 3));
  expectExact("gman::transformDirection", v, {2, 6, 12});
}

template <typename T> gman::Matrix4<T> diagonalMatrix(T a, T b, T c, T d) {
  auto m = gman::Matrix4<T>();
  m[0][0] = a;
  m[1][1] = b;
  m[2][2] = c;
  m[3][3] = d;
  return m;
}

template <typename T> std::vector<T> diagonalEntries(T a, T b, T c, T d) {
  return {a, 0, 0, 0, 0, b, 0, 0, 0, 0, c, 0, 0, 0, 0, d};
}

template <typename T> gman::Matrix4<T> firstWitness() {
  return diagonalMatrix<T>(reciprocal<T, 3>, reciprocal<T, 7>, reciprocal<T, 11>, 1);
}

template <typename T> gman::Matrix4<T> secondWitness() {
  return diagonalMatrix<T>(reciprocal<T, 5>, reciprocal<T, 13>, reciprocal<T, 17>, 1);
}

template <typename T> void testMultiplyMatrixRoundsAtItsOwnPrecision() {
  expectClose(label<T>("operator*(Matrix4) of non-dyadic entries"), firstWitness<T>() * secondWitness<T>(),
              diagonalEntries<T>(T(1) / 15, T(1) / 91, T(1) / 187, 1), 2);
}

template <typename T> void testMultiplyAssignMatrixRoundsAtItsOwnPrecision() {
  auto m = firstWitness<T>();
  m *= secondWitness<T>();
  expectClose(label<T>("operator*=(Matrix4) of non-dyadic entries"), m,
              diagonalEntries<T>(T(1) / 15, T(1) / 91, T(1) / 187, 1), 2);
}

template <typename T> void testAdditionRoundsAtItsOwnPrecision() {
  expectClose(label<T>("operator+(Matrix4) of non-dyadic entries"), firstWitness<T>() + secondWitness<T>(),
              diagonalEntries<T>(T(8) / 15, T(20) / 91, T(28) / 187, 2), 2);
}

template <typename T> void testAddAssignRoundsAtItsOwnPrecision() {
  auto m = firstWitness<T>();
  m += secondWitness<T>();
  expectClose(label<T>("operator+=(Matrix4) of non-dyadic entries"), m,
              diagonalEntries<T>(T(8) / 15, T(20) / 91, T(28) / 187, 2), 2);
}

template <typename T> void testMultiplyScalarRoundsAtItsOwnPrecision() {
  expectClose(label<T>("operator*(T) of non-dyadic entries"), firstWitness<T>() * reciprocal<T, 19>,
              diagonalEntries<T>(T(1) / 57, T(1) / 133, T(1) / 209, T(1) / 19), 2);
}

template <typename T> void testMultiplyAssignScalarRoundsAtItsOwnPrecision() {
  auto m = firstWitness<T>();
  m *= reciprocal<T, 19>;
  expectClose(label<T>("operator*=(T) of non-dyadic entries"), m,
              diagonalEntries<T>(T(1) / 57, T(1) / 133, T(1) / 209, T(1) / 19), 2);
}

template <typename T> void runAll() {
  testDefaultConstructorIsTheIdentity<T>();
  testArrayConstructorFromRtFloat<T>();
  testArrayConstructorFromPartnerReal<T>();
  testConvertingConstructor<T>();
  testCopyConstruct<T>();
  testCopyConstructIsIndependent<T>();
  testCopyAssign<T>();
  testMoveConstruct<T>();
  testMoveAssign<T>();
  testIdentity<T>();
  testConcatAppliesTheArgumentAfterTheReceiver<T>();
  testConcatTheOtherWay<T>();
  testPersp<T>();
  testPerspRoundsAtItsOwnPrecision<T>();
  testTrans<T>();
  testRotQuarterTurnAboutZ<T>();
  testRotNormalizesItsAxis<T>();
  testRotThirdTurnAboutTheDiagonal<T>();
  testScale<T>();
  testSkew<T>();
  testSkewRoundsAtItsOwnPrecision<T>();
  testSkewTooLargeThrows<T>();
  testPrjPersp<T>();
  testPrjPerspRoundsAtItsOwnPrecision<T>();
  testPrjOrtho<T>();
  testPrjOrthoRoundsAtItsOwnPrecision<T>();
  testDeterminantOfTheIdentity<T>();
  testDeterminantOfAScale<T>();
  testDeterminantOfADenseMatrix<T>();
  testDeterminantOfASingularMatrix<T>();
  testInvertAScale<T>();
  testInvertWithAPowerOfTwoDeterminant<T>();
  testInvertRoundsAtItsOwnPrecision<T>();
  testInvertASingularMatrixThrows<T>();
  testMultiplyScalar<T>();
  testMultiplyScalarLeavesItsOperand<T>();
  testMultiplyAssignScalar<T>();
  testMultiplyMatrix<T>();
  testMultiplyMatrixTheOtherWay<T>();
  testMultiplyMatrixLeavesItsOperands<T>();
  testMultiplyAssignMatrix<T>();
  testMultiplyAssignSelf<T>();
  testAddition<T>();
  testAddAssign<T>();
  testAssign<T>();
  testSetBasisFromRtFloat<T>();
  testSetBasisFromPartnerReal<T>();
  testIndexRead<T>();
  testIndexReadConst<T>();
  testIndexWrite<T>();
  testGet<T>();
  testGetRefersToTheMatrixStorage<T>();
  testP3mTranslates<T>();
  testP3mTransformsEachPoint<T>();
  testP3mDividesByW<T>();
  testP3mDividesByWRoundsAtItsOwnPrecision<T>();
  testP3mLeavesAPointWhoseWIsZero<T>();
  testP4m<T>();
  testP4mTransformsEachPoint<T>();
  testP4mKeepsW<T>();
  testMultiplyMatrixRoundsAtItsOwnPrecision<T>();
  testMultiplyAssignMatrixRoundsAtItsOwnPrecision<T>();
  testAdditionRoundsAtItsOwnPrecision<T>();
  testAddAssignRoundsAtItsOwnPrecision<T>();
  testMultiplyScalarRoundsAtItsOwnPrecision<T>();
  testMultiplyAssignScalarRoundsAtItsOwnPrecision<T>();
}

} // namespace

int main() {
  runAll<RtFloat>();
  runAll<gman::PartnerReal>();
  testTransformPoint();
  testTransformDirection();
  return checkSummary("matrix4template: all checks passed");
}
