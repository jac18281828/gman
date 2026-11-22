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
 * gman::InvertibleMatrix carries a placement and its inverse. `of` holds a
 * value exactly when the matrix inverts, forward() times backward() is the
 * identity, inverse() swaps the halves, and a default pair is the identity.
 * Its transformPoint and transformDirection equal the GMANMatrix4 forms on
 * forward(). Its transformNormal applies forward()'s inverse transpose: for
 * tangents t1 and t2 and n = t1 x t2, det(M) * transformNormal(M, n) equals
 * (t1 M) x (t2 M), the identity (aM) x (bM) = det(M) (a x b) M^-T in the
 * row-vector convention. The identity fixes direction, sign and magnitude,
 * and fails if the transpose is dropped under a shear or a rotation.
 */

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "check.h"
#include "gmaninvertiblematrix.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanvector.h"

namespace {

constexpr float kTolerance = 1e-5f;

bool matricesEqual(GMANMatrix4 const& a, GMANMatrix4 const& b) {
  for (std::size_t i = 0; i < 4; ++i)
    for (std::size_t j = 0; j < 4; ++j)
      if (a[i][j] != b[i][j])
        return false;
  return true;
}

bool isIdentity(GMANMatrix4 const& m, float tolerance) {
  for (std::size_t i = 0; i < 4; ++i)
    for (std::size_t j = 0; j < 4; ++j)
      if (std::fabs(m[i][j] - (i == j ? 1.0f : 0.0f)) > tolerance)
        return false;
  return true;
}

bool vectorsEqual(GMANVector const& a, GMANVector const& b) {
  return a.getX() == b.getX() && a.getY() == b.getY() && a.getZ() == b.getZ();
}

bool withinRelative(float actual, float expected) {
  return std::fabs(actual - expected) <= kTolerance * std::fmax(1.0f, std::fabs(expected));
}

GMANMatrix4 nonUniformScale() {
  GMANMatrix4 m;
  m.scale(2.0f, 0.5f, 3.0f);
  return m;
}

GMANMatrix4 shear() {
  GMANMatrix4 m;
  m[1][0] = 0.8f;
  m[0][2] = 0.5f;
  return m;
}

GMANMatrix4 rotationAboutNonAxis() {
  GMANMatrix4 m;
  m.rot(0.7f, 1.0f, 2.0f, 3.0f);
  return m;
}

GMANMatrix4 composition() {
  GMANMatrix4 m = nonUniformScale();
  m.concat(shear());
  m.concat(rotationAboutNonAxis());
  m.trans(4.0f, -2.0f, 7.0f);
  return m;
}

struct NormalCase {
  std::string name;
  GMANMatrix4 matrix;
};

std::vector<NormalCase> normalCases() {
  return {{"scale", nonUniformScale()},
          {"shear", shear()},
          {"rotation", rotationAboutNonAxis()},
          {"composition", composition()}};
}

struct TangentPair {
  std::string name;
  GMANVector t1;
  GMANVector t2;
};

void checkNormalRule(NormalCase const& matrixCase, TangentPair const& pair) {
  std::optional<gman::InvertibleMatrix> const placement = gman::InvertibleMatrix::of(matrixCase.matrix);
  std::string const label = matrixCase.name + ", " + pair.name + " tangents";
  if (!placement) {
    check(false, "normal rule: " + label + " inverts");
    return;
  }

  GMANMatrix4 mutableCopy = matrixCase.matrix;
  float const det = mutableCopy.determinant();
  GMANVector const n = pair.t1.cross(pair.t2);
  GMANVector const actual = gman::transformNormal(*placement, n) * det;
  GMANVector const expected =
      gman::transformDirection(matrixCase.matrix, pair.t1).cross(gman::transformDirection(matrixCase.matrix, pair.t2));

  bool const agrees = withinRelative(actual.getX(), expected.getX()) &&
                      withinRelative(actual.getY(), expected.getY()) && withinRelative(actual.getZ(), expected.getZ());
  check(agrees, "normal rule: " + label + " det * transformNormal equals the tangents' cross product");
}

void checkOf() {
  GMANMatrix4 singular;
  singular.scale(1.0f, 1.0f, 0.0f);
  check(!gman::InvertibleMatrix::of(singular).has_value(), "of: Scale 1 1 0 is empty");

  GMANMatrix4 const input = composition();
  std::optional<gman::InvertibleMatrix> const placement = gman::InvertibleMatrix::of(input);
  check(placement.has_value() && matricesEqual(placement->forward(), input),
        "of: an invertible matrix keeps its forward half");
}

void checkProduct() {
  for (NormalCase const& matrixCase : normalCases()) {
    std::optional<gman::InvertibleMatrix> const placement = gman::InvertibleMatrix::of(matrixCase.matrix);
    check(placement.has_value() && isIdentity(placement->forward() * placement->backward(), kTolerance),
          "forward() times backward() is the identity: " + matrixCase.name);
  }
}

void checkInverse() {
  GMANMatrix4 const input = composition();
  std::optional<gman::InvertibleMatrix> const placement = gman::InvertibleMatrix::of(input);
  if (!placement) {
    check(false, "inverse: composition inverts");
    return;
  }

  gman::InvertibleMatrix const swapped = placement->inverse();
  check(matricesEqual(swapped.forward(), placement->backward()) &&
            matricesEqual(swapped.backward(), placement->forward()),
        "inverse() swaps the halves");

  gman::InvertibleMatrix const restored = swapped.inverse();
  check(matricesEqual(restored.forward(), placement->forward()) &&
            matricesEqual(restored.backward(), placement->backward()),
        "inverse().inverse() equals the original");
}

void checkDefault() {
  gman::InvertibleMatrix const identity;
  check(matricesEqual(identity.forward(), GMANMatrix4()) && matricesEqual(identity.backward(), GMANMatrix4()),
        "the default pair is two identities");
}

void checkPointAndDirection() {
  GMANMatrix4 const input = composition();
  std::optional<gman::InvertibleMatrix> const placement = gman::InvertibleMatrix::of(input);
  if (!placement) {
    check(false, "point and direction: composition inverts");
    return;
  }

  GMANPoint const p(1.5f, -2.0f, 0.25f);
  GMANPoint const viaPair = gman::transformPoint(*placement, p);
  GMANPoint const viaMatrix = gman::transformPoint(placement->forward(), p);
  check(viaPair.getX() == viaMatrix.getX() && viaPair.getY() == viaMatrix.getY() && viaPair.getZ() == viaMatrix.getZ(),
        "transformPoint on the pair equals the GMANMatrix4 form on forward()");

  GMANVector const v(0.3f, -1.2f, 0.8f);
  check(vectorsEqual(gman::transformDirection(*placement, v), gman::transformDirection(placement->forward(), v)),
        "transformDirection on the pair equals the GMANMatrix4 form on forward()");
}

void checkNormalRules() {
  std::vector<TangentPair> const pairs = {{"axis", GMANVector(1.0f, 0.0f, 0.0f), GMANVector(0.0f, 1.0f, 0.0f)},
                                          {"generic", GMANVector(0.3f, -1.2f, 0.8f), GMANVector(1.1f, 0.4f, -0.6f)}};
  for (NormalCase const& matrixCase : normalCases())
    for (TangentPair const& pair : pairs)
      checkNormalRule(matrixCase, pair);
}

} // namespace

int main() {
  checkOf();
  checkProduct();
  checkInverse();
  checkDefault();
  checkPointAndDirection();
  checkNormalRules();
  return checkSummary(
      "gman::InvertibleMatrix pairs a placement with its inverse and transforms normals by the inverse transpose");
}
