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
#include "gmanmatrix4.h"
#include "gmanpoint.h"
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
  RtFloat x, y, z;

public:
  GMANVector() : x(0.0), y(0.0), z(0.0) {}

  GMANVector(RtFloat xv, RtFloat yv, RtFloat zv) : x(xv), y(yv), z(zv) {}

  GMANVector(const RtFloat* pts) : x(pts[X]), y(pts[Y]), z(pts[Z]) {}

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
   * -Werror=deprecated-copy. It is memberwise on the plain coordinates, which is
   * what the implicit one already did, so default it explicitly. Copy
   * assignment is user-provided further down. */
  GMANVector(const GMANVector&) = default;

  RtFloat getX(RtVoid) const { return x; }
  RtVoid setX(RtFloat xv) { x = xv; }

  RtFloat getY(RtVoid) const { return y; }
  RtVoid setY(RtFloat yv) { y = yv; }

  RtFloat getZ(RtVoid) const { return z; }
  RtVoid setZ(RtFloat zv) { z = zv; }

  RtFloat& operator[](size_t i) {
    if (i == X) {
      return x;
    }
    if (i == Y) {
      return y;
    }
    return z;
  }

  const RtFloat& operator[](size_t i) const {
    if (i == X) {
      return x;
    }
    if (i == Y) {
      return y;
    }
    return z;
  }

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

  RtFloat magnitude(RtVoid) { return std::sqrt(x * x + y * y + z * z); }

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
    rc += x * v.x;
    rc += y * v.y;
    rc += z * v.z;
    return rc;
  }

  GMANVector cross(const GMANVector& v) const {
    GMANVector result(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    return result;
  }

  /* operators */
  GMANVector operator+(GMANVector const& v) const { return GMANVector(x + v.x, y + v.y, z + v.z); }

  GMANVector operator-(GMANVector const& v) const { return GMANVector(x - v.x, y - v.y, z - v.z); }

  GMANVector operator-() const { return GMANVector(-x, -y, -z); }

  GMANVector operator+(RtFloat f) const { return GMANVector(x + f, y + f, z + f); }
  GMANVector operator-(RtFloat f) const { return GMANVector(x - f, y - f, z - f); }
  GMANVector operator*(RtFloat f) const { return GMANVector(x * f, y * f, z * f); }
  GMANVector operator/(RtFloat f) const { return GMANVector(x / f, y / f, z / f); }

  GMANVector& operator=(RtFloat f) {
    x = y = z = f;
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
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
  }
  GMANVector& operator-=(GMANVector const& v) {
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
  }
  GMANVector& operator*=(GMANVector const& v) {
    x *= v.x;
    y *= v.y;
    z *= v.z;
    return *this;
  }
  GMANVector& operator/=(GMANVector const& v) {
    x /= v.x;
    y /= v.y;
    z /= v.z;
    return *this;
  }

  GMANVector& operator+=(RtFloat f) {
    x += f;
    y += f;
    z += f;
    return *this;
  }
  GMANVector& operator-=(RtFloat f) {
    x -= f;
    y -= f;
    z -= f;
    return *this;
  }
  GMANVector& operator*=(RtFloat f) {
    x *= f;
    y *= f;
    z *= f;
    return *this;
  }
  GMANVector& operator/=(RtFloat f) {
    x /= f;
    y /= f;
    z /= f;
    return *this;
  }

  GMANVector& operator*=(const GMANMatrix4& m);

  bool operator<(const GMANVector& v) const { return x < v.x || y < v.y || z < v.z; }
};
