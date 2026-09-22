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

#include "gmanerror.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "ri.h"

class GMANPoint;
class GMANVector;

class GMAN_EXPORT GMANMatrix4 {
private:
  RtMatrix mtrx;

public:
  GMANMatrix4();
  GMANMatrix4(RtMatrix m);

  RtVoid identity();
  RtVoid concat(const GMANMatrix4& m);
  RtVoid persp(RtFloat fov);
  RtVoid trans(RtFloat dx, RtFloat dy, RtFloat dz);
  RtVoid rot(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz);
  RtVoid scale(RtFloat sx, RtFloat sy, RtFloat sz);
  RtVoid skew(RtFloat angle, GMANVector& a, GMANVector& b);

  RtVoid prjPersp(RtFloat fov, RtFloat nearOne, RtFloat farOne);
  RtVoid prjOrtho(RtFloat nearOne, RtFloat farOne);

  RtFloat determinant(RtVoid);
  RtVoid invert(RtVoid);

  RtVoid p3m(RtInt n, RtFloat* src, RtFloat* dest) const;
  RtVoid p4m(RtInt n, RtFloat* src, RtFloat* dest) const;

  GMANMatrix4 operator*(RtFloat f) const;
  GMANMatrix4& operator*=(RtFloat f);
  GMANMatrix4 operator*(const GMANMatrix4& m) const;
  GMANMatrix4& operator*=(const GMANMatrix4& m);
  GMANMatrix4 operator+(const GMANMatrix4& m) const;
  GMANMatrix4& operator+=(const GMANMatrix4& m);

  GMANMatrix4& operator=(const RtMatrix& m);
  GMANMatrix4(const GMANMatrix4& m) = default;

  GMANMatrix4& operator=(const GMANMatrix4& m);

  GMANMatrix4& assign(const GMANMatrix4& m);

  RtVoid setBasis(RtBasis& b);

  RtFloat* operator[](size_t i) { return mtrx[i]; }

  const RtFloat* operator[](size_t i) const { return mtrx[i]; }

  const RtMatrix& get(RtVoid) const { return mtrx; }
};

namespace gman {

// A ray tracer's own transform of a point, a direction or a normal, needed
// wherever a ray moves between camera and object space. m applies as a
// row vector, p * m (AGENTS.md's CTM convention), the opposite of
// GMANPoint::operator*'s m * p; a caller reaching for that operator here
// instead would silently transform by the wrong side of the matrix.

// Delegates to GMANMatrix4::p3m. Perspective-divides when m's homogeneous
// w is neither 0 nor 1.
GMAN_EXPORT GMANPoint transformPoint(GMANMatrix4 const& m, GMANPoint const& p);

// A direction's homogeneous w is 0, so the translation row (row 3) drops
// out and no perspective divide applies.
GMAN_EXPORT GMANVector transformDirection(GMANMatrix4 const& m, GMANVector const& v);

// A normal transforms by the inverse transpose of the point transform, not
// by the point or direction transform itself: callers pass m already
// inverted. The two productions agree whenever m is symmetric, as a
// translation's or a scale's linear part is, and diverge under a rotation.
GMAN_EXPORT GMANVector transformNormal(GMANMatrix4 const& m, GMANVector const& v);

} // namespace gman
