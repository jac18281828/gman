/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  November 2000  First release

  April 2002:   JAC - major rewrite of this code.

  ---------------------------------------------------------
  4x4 matrix
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

#include <cmath>
#include <concepts>

#include "gmanerror.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

template <typename T> Matrix4<T>::Matrix4() { identity(); }

template <typename T>
Matrix4<T>::Matrix4(RtMatrix m)
  requires std::same_as<T, RtFloat>
{
  for (RtInt i = 0; i < 4; i++)
    for (RtInt j = 0; j < 4; j++)
      mtrx[i][j] = m[i][j];
}

template <typename T> RtVoid Matrix4<T>::identity(RtVoid) {
  for (RtInt i = 0; i < 4; i++)
    for (RtInt j = 0; j < 4; j++)
      if (i == j)
        mtrx[i][j] = 1.0;
      else
        mtrx[i][j] = 0.0;
}

template <typename T> Matrix4<T>& Matrix4<T>::operator=(Matrix4 const& m) {
  for (RtInt i = 0; i < 4; i++) {
    for (RtInt j = 0; j < 4; j++) {
      mtrx[i][j] = m[i][j];
    }
  }

  return *this;
}

template <typename T> RtVoid Matrix4<T>::concat(Matrix4 const& m) { *this *= m; }

// near=1 far=+infinity (The RenderMan Companion)
template <typename T> RtVoid Matrix4<T>::persp(T fov) {
  Matrix4 m;
  T t = tan((fov / 2) * DEGTORAD);
  m.mtrx[0][3] = t;
  m.mtrx[1][3] = -t;
  m.mtrx[2][3] = t;
  m.mtrx[3][3] = 0.0;
  concat(m);
}

template <typename T> RtVoid Matrix4<T>::trans(T dx, T dy, T dz) {
  Matrix4 m;
  m.mtrx[3][0] = dx;
  m.mtrx[3][1] = dy;
  m.mtrx[3][2] = dz;
  concat(m);
}

template <typename T> RtVoid Matrix4<T>::rot(T angle, T dx, T dy, T dz) {
  Matrix4 m;
  T s, c, omc, xx, xy, xz, yy, yz, zz;

  // Normalize the axis
  T mag = sqrt(dx * dx + dy * dy + dz * dz);
  dx /= mag;
  dy /= mag;
  dz /= mag;

  s = sin(angle);
  c = cos(angle);
  omc = 1 - c;
  xx = dx * dx;
  xy = dx * dy;
  xz = dx * dz;
  yy = dy * dy;
  yz = dy * dz;
  zz = dz * dz;

  m.mtrx[0][0] = omc * xx + c;
  m.mtrx[1][0] = omc * xy - dz * s;
  m.mtrx[2][0] = omc * xz + dy * s;

  m.mtrx[0][1] = omc * xy + dz * s;
  m.mtrx[1][1] = omc * yy + c;
  m.mtrx[2][1] = omc * yz - dx * s;

  m.mtrx[0][2] = omc * xz - dy * s;
  m.mtrx[1][2] = omc * yz + dx * s;
  m.mtrx[2][2] = omc * zz + c;

  concat(m);
}

template <typename T> RtVoid Matrix4<T>::scale(T sx, T sy, T sz) {
  Matrix4 m;
  m.mtrx[0][0] = sx;
  m.mtrx[1][1] = sy;
  m.mtrx[2][2] = sz;
  concat(m);
}

template <typename T> RtVoid Matrix4<T>::skew(T angle, Vector<T>& a, Vector<T>& b) {
  Matrix4 m;
  Vector<T> n1, n2;
  Vector<T> a1, a2;
  T an1, an2;
  T rx, ry;
  T alpha;

  n2 = b.normalize();
  a1 = n2 * a.dot(n2);
  a2 = a - a1;
  n1 = a2.normalize();

  an1 = a.dot(n1);
  an2 = a.dot(n2);

  rx = an1 * cos(angle * DEGTORAD) - an2 * sin(angle * DEGTORAD);
  ry = an1 * sin(angle * DEGTORAD) + an2 * cos(angle * DEGTORAD);

  if (rx <= 0.0) {
    GMANError error(RIE_CONSISTENCY, RIE_ERROR, "Skew: angle too large");
    throw error;
  }

  // A parallel to B??
  if (an1 == 0) {
    alpha = 0;
  } else {
    alpha = ry / rx - an2 / an1;
  }

  m.mtrx[0][0] = n1.getX() * n2.getX() * alpha + 1.0;
  m.mtrx[1][0] = n1.getY() * n2.getX() * alpha;
  m.mtrx[2][0] = n1.getZ() * n2.getX() * alpha;

  m.mtrx[0][1] = n1.getX() * n2.getY() * alpha;
  m.mtrx[1][1] = n1.getY() * n2.getY() * alpha + 1.0;
  m.mtrx[2][1] = n1.getZ() * n2.getY() * alpha;

  m.mtrx[0][2] = n1.getX() * n2.getZ() * alpha;
  m.mtrx[1][2] = n1.getY() * n2.getZ() * alpha;
  m.mtrx[2][2] = n1.getZ() * n2.getZ() * alpha + 1.0;

  concat(m);
}

// RiProjection perspective and orthographic matrix.
//
// Consumed via GMANVector4::projTransform / GMANPoint::operator*=, which
// both compute out[i] = row_i(M).(x,y,z) + M[i][3] -- the convention already
// in use for the projection stage (see persp(), above, which the RiPerspective
// transform command builds the same way). w is placed in row 3 so it carries
// z directly (RenderMan's camera looks down +z) instead of a constant.
template <typename T> RtVoid Matrix4<T>::prjPersp(T fov, T nearDist, T farDist) {
  T invT = 1.0 / tan((fov / 2) * DEGTORAD);

  identity();
  mtrx[0][0] = invT;
  mtrx[1][1] = invT;
  mtrx[2][2] = (farDist + nearDist) / (farDist - nearDist);
  mtrx[2][3] = -2.0 * farDist * nearDist / (farDist - nearDist);
  mtrx[3][2] = 1.0;
  mtrx[3][3] = 0.0;
}

template <typename T> RtVoid Matrix4<T>::prjOrtho(T nearDist, T farDist) {
  identity();
  mtrx[2][2] = 2.0 / (farDist - nearDist);
  mtrx[2][3] = -(farDist + nearDist) / (farDist - nearDist);
}

namespace {

// The twelve 2x2 cofactors shared by determinant() and invert(): a0..a5
// pair rows 0-1, b0..b5 pair rows 2-3. determinant is their standard
// pairing, a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0.
template <typename T> struct Cofactors {
  T a0, a1, a2, a3, a4, a5;
  T b0, b1, b2, b3, b4, b5;
  T determinant;
};

template <typename T> Cofactors<T> cofactorsOf(T const (&m)[4][4]) {
  Cofactors<T> c;
  c.a0 = m[0][0] * m[1][1] - m[0][1] * m[1][0];
  c.a1 = m[0][0] * m[1][2] - m[0][2] * m[1][0];
  c.a2 = m[0][0] * m[1][3] - m[0][3] * m[1][0];
  c.a3 = m[0][1] * m[1][2] - m[0][2] * m[1][1];
  c.a4 = m[0][1] * m[1][3] - m[0][3] * m[1][1];
  c.a5 = m[0][2] * m[1][3] - m[0][3] * m[1][2];
  c.b0 = m[2][0] * m[3][1] - m[2][1] * m[3][0];
  c.b1 = m[2][0] * m[3][2] - m[2][2] * m[3][0];
  c.b2 = m[2][0] * m[3][3] - m[2][3] * m[3][0];
  c.b3 = m[2][1] * m[3][2] - m[2][2] * m[3][1];
  c.b4 = m[2][1] * m[3][3] - m[2][3] * m[3][1];
  c.b5 = m[2][2] * m[3][3] - m[2][3] * m[3][2];
  c.determinant = c.a0 * c.b5 - c.a1 * c.b4 + c.a2 * c.b3 + c.a3 * c.b2 - c.a4 * c.b1 + c.a5 * c.b0;
  return c;
}

} // namespace

template <typename T> T Matrix4<T>::determinant() { return cofactorsOf(mtrx).determinant; }

// Adjugate / determinant, via the same 2x2-cofactor pairing determinant()
// uses. Throws on a singular matrix.
template <typename T> RtVoid Matrix4<T>::invert() {
  auto const& m = mtrx;
  const auto c = cofactorsOf(m);

  const auto d = c.determinant;
  if (d == 0.0) {
    GMANError error(RIE_MATH, RIE_ERROR, "Cannot invert matrix");
    throw error;
  }
  const T id = 1.0 / d;

  Matrix4 inv;
  inv.mtrx[0][0] = (m[1][1] * c.b5 - m[1][2] * c.b4 + m[1][3] * c.b3) * id;
  inv.mtrx[0][1] = (-m[0][1] * c.b5 + m[0][2] * c.b4 - m[0][3] * c.b3) * id;
  inv.mtrx[0][2] = (m[3][1] * c.a5 - m[3][2] * c.a4 + m[3][3] * c.a3) * id;
  inv.mtrx[0][3] = (-m[2][1] * c.a5 + m[2][2] * c.a4 - m[2][3] * c.a3) * id;
  inv.mtrx[1][0] = (-m[1][0] * c.b5 + m[1][2] * c.b2 - m[1][3] * c.b1) * id;
  inv.mtrx[1][1] = (m[0][0] * c.b5 - m[0][2] * c.b2 + m[0][3] * c.b1) * id;
  inv.mtrx[1][2] = (-m[3][0] * c.a5 + m[3][2] * c.a2 - m[3][3] * c.a1) * id;
  inv.mtrx[1][3] = (m[2][0] * c.a5 - m[2][2] * c.a2 + m[2][3] * c.a1) * id;
  inv.mtrx[2][0] = (m[1][0] * c.b4 - m[1][1] * c.b2 + m[1][3] * c.b0) * id;
  inv.mtrx[2][1] = (-m[0][0] * c.b4 + m[0][1] * c.b2 - m[0][3] * c.b0) * id;
  inv.mtrx[2][2] = (m[3][0] * c.a4 - m[3][1] * c.a2 + m[3][3] * c.a0) * id;
  inv.mtrx[2][3] = (-m[2][0] * c.a4 + m[2][1] * c.a2 - m[2][3] * c.a0) * id;
  inv.mtrx[3][0] = (-m[1][0] * c.b3 + m[1][1] * c.b1 - m[1][2] * c.b0) * id;
  inv.mtrx[3][1] = (m[0][0] * c.b3 - m[0][1] * c.b1 + m[0][2] * c.b0) * id;
  inv.mtrx[3][2] = (-m[3][0] * c.a3 + m[3][1] * c.a1 - m[3][2] * c.a0) * id;
  inv.mtrx[3][3] = (m[2][0] * c.a3 - m[2][1] * c.a1 + m[2][2] * c.a0) * id;

  *this = inv;
}

// Row-vector (p*M) transform of a non-homogeneous point: dest_j =
// sum_i src_i*M[i][j] + M[3][j], matching trans()/rot()/scale()/concat(),
// which store translation in row 3. Divides by the resulting w so a
// (rare) projective matrix passed here still produces a valid point.
template <typename T> RtVoid Matrix4<T>::p3m(RtInt nbpts, T* src, T* dest) const {
  for (RtInt i = 0; i < nbpts * 3; i += 3) {
    const auto x = src[0 + i];
    const auto y = src[1 + i];
    const auto z = src[2 + i];
    auto rx = x * mtrx[0][0] + y * mtrx[1][0] + z * mtrx[2][0] + mtrx[3][0];
    auto ry = x * mtrx[0][1] + y * mtrx[1][1] + z * mtrx[2][1] + mtrx[3][1];
    auto rz = x * mtrx[0][2] + y * mtrx[1][2] + z * mtrx[2][2] + mtrx[3][2];
    const auto rw = x * mtrx[0][3] + y * mtrx[1][3] + z * mtrx[2][3] + mtrx[3][3];
    if (rw != 1.0 && rw != 0.0) {
      const T invw = 1.0 / rw;
      rx *= invw;
      ry *= invw;
      rz *= invw;
    }
    dest[0 + i] = rx;
    dest[1 + i] = ry;
    dest[2 + i] = rz;
  }
}

// Homogeneous counterpart of p3m: carries w through unnormalized, for
// callers (e.g. normal transforms) that need the raw row-vector product
// rather than a perspective-divided point.
template <typename T> RtVoid Matrix4<T>::p4m(RtInt nbpts, T* src, T* dest) const {
  for (RtInt i = 0; i < nbpts * 4; i += 4) {
    const auto x = src[0 + i];
    const auto y = src[1 + i];
    const auto z = src[2 + i];
    const auto w = src[3 + i];
    dest[0 + i] = x * mtrx[0][0] + y * mtrx[1][0] + z * mtrx[2][0] + w * mtrx[3][0];
    dest[1 + i] = x * mtrx[0][1] + y * mtrx[1][1] + z * mtrx[2][1] + w * mtrx[3][1];
    dest[2 + i] = x * mtrx[0][2] + y * mtrx[1][2] + z * mtrx[2][2] + w * mtrx[3][2];
    dest[3 + i] = x * mtrx[0][3] + y * mtrx[1][3] + z * mtrx[2][3] + w * mtrx[3][3];
  }
}
template <typename T> Matrix4<T> Matrix4<T>::operator*(T f) const {
  Matrix4 res(*this);

  res *= f;

  return res;
}

// Scales every one of the 16 entries by f.
template <typename T> Matrix4<T>& Matrix4<T>::operator*=(T f) {
  for (RtInt i = 0; i < 4; i++)
    for (RtInt j = 0; j < 4; j++)
      mtrx[i][j] *= f;

  return *this;
}

template <typename T> Matrix4<T> Matrix4<T>::operator*(Matrix4 const& m) const {
  Matrix4 res(*this);

  res *= m;
  return res;
}

template <typename T> Matrix4<T>& Matrix4<T>::operator*=(Matrix4 const& m) {
  Matrix4 res;

  for (RtInt i = 0; i < 4; i++) {
    for (RtInt j = 0; j < 4; j++) {
      res[i][j] = 0.0;
      for (RtInt k = 0; k < 4; k++) {
        res[i][j] += mtrx[i][k] * m[k][j];
      }
    }
  }
  *this = res;
  return *this;
}

template <typename T> Matrix4<T> Matrix4<T>::operator+(Matrix4 const& m) const {
  Matrix4 res(*this);

  res += m;
  return res;
}

template <typename T> Matrix4<T>& Matrix4<T>::operator+=(Matrix4 const& m) {
  for (RtInt i = 0; i < 4; i++) {
    for (RtInt j = 0; j < 4; j++) {
      mtrx[i][j] += m.mtrx[i][j];
    }
  }
  return *this;
}

template <typename T>
RtVoid Matrix4<T>::setBasis(RtBasis& b)
  requires std::same_as<T, RtFloat>
{
  for (RtInt i = 0; i < 4; i++) {
    for (RtInt j = 0; j < 4; j++) {
      mtrx[i][j] = b[i][j];
    }
  }
}

template <typename T> Matrix4<T>& Matrix4<T>::assign(Matrix4 const& m) {
  *this = m;
  return *this;
}

GMANPoint transformPoint(GMANMatrix4 const& m, GMANPoint const& p) {
  RtFloat src[3] = {p.getX(), p.getY(), p.getZ()};
  RtFloat dest[3];
  m.p3m(1, src, dest);
  return GMANPoint(dest[0], dest[1], dest[2]);
}

GMANVector transformDirection(GMANMatrix4 const& m, GMANVector const& v) {
  RtFloat const x = v.getX(), y = v.getY(), z = v.getZ();
  return GMANVector(x * m[0][0] + y * m[1][0] + z * m[2][0], x * m[0][1] + y * m[1][1] + z * m[2][1],
                    x * m[0][2] + y * m[1][2] + z * m[2][2]);
}

} // namespace gman

template class gman::Matrix4<RtFloat>;
template class gman::Matrix4<gman::PartnerReal>;
