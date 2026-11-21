/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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

#include <cmath>
#include <cstddef>

#include "gman/export.h"
#include "gmanlog.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmantypes.h"
#include "ri.h"

/*
 * RenderMan API GMANVector
 *
 * A physical simulation vector, i.e., magnitude
 * and direction, not to be confused with the
 * stl "vector" template
 *
 */

class GMAN_EXPORT GMANVector {
public:
  // public type
  typedef enum { X = 0, Y = 1, Z = 2, NCOORDS } CoordType;

protected:
  // point spatial coordinates
  RtVector vec;

public:
  GMANVector() {
    for (RtInt i = 0; i < NCOORDS; i++) {
      vec[i] = 0.0;
    }
  }

  GMANVector(RtFloat x, RtFloat y, RtFloat z) {
    setX(x);
    setY(y);
    setZ(z);
  }

  GMANVector(const RtFloat* pts) {
    for (int i = 0; i < NCOORDS; i++) {
      vec[i] = pts[i];
    }
  }

  GMANVector(const GMANPoint& p) { *this = p; }

  /* construct a vector from two points V = b - a */
  GMANVector(const GMANPoint& a, const GMANPoint& b) {
    setX(b.getX() - a.getX());
    setY(b.getY() - a.getY());
    setZ(b.getZ() - a.getZ());
  }

  // dtor
  ~GMANVector() {}

  /* The user-declared destructor above deprecates the implicit copy
   * constructor (C++11 [depr.impldec]); gcc rejects it under
   * -Werror=deprecated-copy. It is memberwise on a plain RtVector, which is
   * what the implicit one already did, so default it explicitly. Copy
   * assignment is user-provided further down. */
  GMANVector(const GMANVector&) = default;

  RtFloat getX(RtVoid) const { return vec[X]; }
  RtVoid setX(RtFloat x) { vec[X] = x; }

  RtFloat getY(RtVoid) const { return vec[Y]; }
  RtVoid setY(RtFloat y) { vec[Y] = y; }

  RtFloat getZ(RtVoid) const { return vec[Z]; }
  RtVoid setZ(RtFloat z) { vec[Z] = z; }

  RtFloat& operator[](size_t i) { return vec[i]; }

  const RtFloat& operator[](size_t i) const { return vec[i]; }

  operator GMANPoint() {
    GMANPoint res(getX(), getY(), getZ());

    return res;
  }

  GMANVector& operator=(const GMANPoint& p) {
    setX(p.getX());
    setY(p.getY());
    setZ(p.getZ());

    return *this;
  }

  RtFloat magnitude(RtVoid) { return std::sqrt(vec[X] * vec[X] + vec[Y] * vec[Y] + vec[Z] * vec[Z]); }

  GMANVector& normalize(RtVoid) {
    RtFloat mag = magnitude();
    if (mag < RI_EPSILON) {
      return *this;
    }
    *this /= mag;
    return *this;
  }

  RtFloat dot(const GMANVector& v) const {
    RtFloat rc = 0.0;
    for (RtInt i = 0; i < NCOORDS; i++) {
      rc += vec[i] * v.vec[i];
    }
    return rc;
  }

  GMANVector cross(const GMANVector& v) const {
    GMANVector result(vec[Y] * v.vec[Z] - vec[Z] * v.vec[Y], vec[Z] * v.vec[X] - vec[X] * v.vec[Z],
                      vec[X] * v.vec[Y] - vec[Y] * v.vec[X]);
    return result;
  }

  /* operators */
  GMANVector operator+(GMANVector const& v) const {
    return GMANVector(vec[X] + v.vec[X], vec[Y] + v.vec[Y], vec[Z] + v.vec[Z]);
  }

  GMANVector operator-(GMANVector const& v) const {
    return GMANVector(vec[X] - v.vec[X], vec[Y] - v.vec[Y], vec[Z] - v.vec[Z]);
  }

  GMANVector operator-() const { return GMANVector(-vec[X], -vec[Y], -vec[Z]); }

  GMANVector operator+(RtFloat f) const { return GMANVector(vec[X] + f, vec[Y] + f, vec[Z] + f); }
  GMANVector operator-(RtFloat f) const { return GMANVector(vec[X] - f, vec[Y] - f, vec[Z] - f); }
  GMANVector operator*(RtFloat f) const { return GMANVector(vec[X] * f, vec[Y] * f, vec[Z] * f); }
  GMANVector operator/(RtFloat f) const { return GMANVector(vec[X] / f, vec[Y] / f, vec[Z] / f); }

  GMANVector& operator=(RtFloat f) {
    vec[X] = vec[Y] = vec[Z] = f;
    return *this;
  }

  // assign a GMANVector
  GMANVector& operator=(const GMANVector& v) {
    setX(v.getX());
    setY(v.getY());
    setZ(v.getZ());

    return *this;
  }

  GMANVector& operator+=(GMANVector const& v) {
    vec[X] += v.vec[X];
    vec[Y] += v.vec[Y];
    vec[Z] += v.vec[Z];
    return *this;
  }
  GMANVector& operator-=(GMANVector const& v) {
    vec[X] -= v.vec[X];
    vec[Y] -= v.vec[Y];
    vec[Z] -= v.vec[Z];
    return *this;
  }
  GMANVector& operator*=(GMANVector const& v) {
    vec[X] *= v.vec[X];
    vec[Y] *= v.vec[Y];
    vec[Z] *= v.vec[Z];
    return *this;
  }
  GMANVector& operator/=(GMANVector const& v) {
    vec[X] /= v.vec[X];
    vec[Y] /= v.vec[Y];
    vec[Z] /= v.vec[Z];
    return *this;
  }

  GMANVector& operator+=(RtFloat f) {
    vec[X] += f;
    vec[Y] += f;
    vec[Z] += f;
    return *this;
  }
  GMANVector& operator-=(RtFloat f) {
    vec[X] -= f;
    vec[Y] -= f;
    vec[Z] -= f;
    return *this;
  }
  GMANVector& operator*=(RtFloat f) {
    vec[X] *= f;
    vec[Y] *= f;
    vec[Z] *= f;
    return *this;
  }
  GMANVector& operator/=(RtFloat f) {
    vec[X] /= f;
    vec[Y] /= f;
    vec[Z] /= f;
    return *this;
  }

  GMANVector& operator*=(const GMANMatrix4& m);

  bool operator<(const GMANVector& v) const {
    for (int i = 0; i < NCOORDS; i++) {
      if (vec[i] < v.vec[i])
        return true;
    }
    return false;
  }
};
