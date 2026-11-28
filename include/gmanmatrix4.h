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

#pragma once

#include <concepts>
#include <cstddef>

#include "gman/export.h"
#include "gmanreal.h"
#include "ri.h"

namespace gman {

template <typename T> class GMAN_EXPORT Point;
template <typename T> class GMAN_EXPORT Vector;

} // namespace gman

using GMANPoint = gman::Point<RtFloat>;
using GMANVector = gman::Vector<RtFloat>;

namespace gman {

// A 4x4 matrix over the real scalar T. Only the RtFloat instance converts to
// and from the RenderMan array types; Matrix4<U> converts explicitly to
// Matrix4<T>.
template <typename T> class GMAN_EXPORT Matrix4 {
private:
  T mtrx[4][4];

public:
  Matrix4();
  Matrix4(RtMatrix m)
    requires std::same_as<T, RtFloat>;

  template <typename U> explicit Matrix4(Matrix4<U> const& m) {
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        mtrx[i][j] = static_cast<T>(m[i][j]);
  }

  void identity();
  void concat(Matrix4 const& m);
  void persp(T fov);
  void trans(T dx, T dy, T dz);
  void rot(T angle, T dx, T dy, T dz);
  void scale(T sx, T sy, T sz);
  void skew(T angle, Vector<T>& a, Vector<T>& b);

  void prjPersp(T fov, T nearOne, T farOne);
  void prjOrtho(T nearOne, T farOne);

  T determinant();
  void invert();

  void p3m(int n, T* src, T* dest) const;
  void p4m(int n, T* src, T* dest) const;

  Matrix4 operator*(T f) const;
  Matrix4& operator*=(T f);
  Matrix4 operator*(Matrix4 const& m) const;
  Matrix4& operator*=(Matrix4 const& m);
  Matrix4 operator+(Matrix4 const& m) const;
  Matrix4& operator+=(Matrix4 const& m);

  Matrix4& operator=(RtMatrix const& m)
    requires std::same_as<T, RtFloat>;
  Matrix4(Matrix4 const& m) = default;

  Matrix4& operator=(Matrix4 const& m);

  Matrix4& assign(Matrix4 const& m);

  void setBasis(RtBasis& b)
    requires std::same_as<T, RtFloat>;

  T* operator[](std::size_t i) { return mtrx[i]; }

  T const* operator[](std::size_t i) const { return mtrx[i]; }

  T const (&get() const)[4][4] { return mtrx; }
};

} // namespace gman

using GMANMatrix4 = gman::Matrix4<RtFloat>;

extern template class gman::Matrix4<RtFloat>;
extern template class gman::Matrix4<gman::PartnerReal>;

namespace gman {

// A ray tracer's own transform of a point or a direction, needed
// wherever a ray moves between camera and object space. m applies as a
// row vector, p * m (AGENTS.md's CTM convention).

// Delegates to GMANMatrix4::p3m. Perspective-divides when m's homogeneous
// w is neither 0 nor 1.
GMAN_EXPORT GMANPoint transformPoint(GMANMatrix4 const& m, GMANPoint const& p);

// A direction's homogeneous w is 0, so the translation row (row 3) drops
// out and no perspective divide applies.
GMAN_EXPORT GMANVector transformDirection(GMANMatrix4 const& m, GMANVector const& v);

} // namespace gman
