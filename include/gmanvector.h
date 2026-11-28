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
#include <type_traits>

#include "gman/export.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "ri.h"

namespace gman {

/*
 * RenderMan API Vector
 *
 * A physical simulation vector, i.e., magnitude
 * and direction, not to be confused with the
 * stl "vector" template. It is a template over
 * the real scalar T; GMANVector names the instance
 * for the RenderMan scalar. A pointer to any
 * arithmetic type builds a Vector, and Vector<U>
 * converts explicitly to Vector<T>.
 *
 */

template <typename T> class GMAN_EXPORT Vector {
public:
  // public type
  typedef enum { X = 0, Y = 1, Z = 2, NCOORDS } CoordType;

protected:
  // point spatial coordinates
  T x, y, z;

public:
  Vector() : x(0.0), y(0.0), z(0.0) {}

  Vector(T xv, T yv, T zv) : x(xv), y(yv), z(zv) {}

  template <typename U>
    requires std::is_arithmetic_v<U>
  explicit Vector(U const* pts) : x(static_cast<T>(pts[X])), y(static_cast<T>(pts[Y])), z(static_cast<T>(pts[Z])) {}

  template <typename U>
  explicit Vector(Vector<U> const& v)
      : x(static_cast<T>(v.getX())), y(static_cast<T>(v.getY())), z(static_cast<T>(v.getZ())) {}

  Vector(Point<T> const& p) { *this = p; }

  /* construct a vector from two points V = b - a */
  Vector(Point<T> const& a, Point<T> const& b) {
    setX(b.getX() - a.getX());
    setY(b.getY() - a.getY());
    setZ(b.getZ() - a.getZ());
  }

  // dtor
  ~Vector() {}

  /* The user-declared destructor above deprecates the implicit copy
   * constructor (C++11 [depr.impldec]); gcc rejects it under
   * -Werror=deprecated-copy. It is memberwise on the plain coordinates, which is
   * what the implicit one already did, so default it explicitly. Copy
   * assignment is user-provided further down. */
  Vector(Vector const&) = default;

  T getX() const { return x; }
  void setX(T xv) { x = xv; }

  T getY() const { return y; }
  void setY(T yv) { y = yv; }

  T getZ() const { return z; }
  void setZ(T zv) { z = zv; }

  T& operator[](std::size_t i) {
    if (i == X) {
      return x;
    }
    if (i == Y) {
      return y;
    }
    return z;
  }

  T const& operator[](std::size_t i) const {
    if (i == X) {
      return x;
    }
    if (i == Y) {
      return y;
    }
    return z;
  }

  operator Point<T>() {
    Point<T> res(getX(), getY(), getZ());

    return res;
  }

  Vector& operator=(Point<T> const& p) {
    setX(p.getX());
    setY(p.getY());
    setZ(p.getZ());

    return *this;
  }

  T magnitude() { return std::sqrt(x * x + y * y + z * z); }

  Vector& normalize() {
    const auto mag = magnitude();
    if (mag < RI_EPSILON) {
      return *this;
    }
    *this /= mag;
    return *this;
  }

  T dot(Vector const& v) const {
    T rc = 0.0;
    rc += x * v.x;
    rc += y * v.y;
    rc += z * v.z;
    return rc;
  }

  Vector cross(Vector const& v) const {
    Vector result(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
    return result;
  }

  /* operators */
  Vector operator+(Vector const& v) const { return Vector(x + v.x, y + v.y, z + v.z); }

  Vector operator-(Vector const& v) const { return Vector(x - v.x, y - v.y, z - v.z); }

  Vector operator-() const { return Vector(-x, -y, -z); }

  Vector operator+(T f) const { return Vector(x + f, y + f, z + f); }
  Vector operator-(T f) const { return Vector(x - f, y - f, z - f); }
  Vector operator*(T f) const { return Vector(x * f, y * f, z * f); }
  Vector operator/(T f) const { return Vector(x / f, y / f, z / f); }

  Vector& operator=(T f) {
    x = y = z = f;
    return *this;
  }

  // assign a Vector
  Vector& operator=(Vector const& v) {
    setX(v.getX());
    setY(v.getY());
    setZ(v.getZ());

    return *this;
  }

  Vector& operator+=(Vector const& v) {
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
  }
  Vector& operator-=(Vector const& v) {
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
  }
  Vector& operator*=(Vector const& v) {
    x *= v.x;
    y *= v.y;
    z *= v.z;
    return *this;
  }
  Vector& operator/=(Vector const& v) {
    x /= v.x;
    y /= v.y;
    z /= v.z;
    return *this;
  }

  Vector& operator+=(T f) {
    x += f;
    y += f;
    z += f;
    return *this;
  }
  Vector& operator-=(T f) {
    x -= f;
    y -= f;
    z -= f;
    return *this;
  }
  Vector& operator*=(T f) {
    x *= f;
    y *= f;
    z *= f;
    return *this;
  }
  Vector& operator/=(T f) {
    x /= f;
    y /= f;
    z /= f;
    return *this;
  }

  Vector& operator*=(Matrix4<T> const& m);

  bool operator<(Vector const& v) const { return x < v.x || y < v.y || z < v.z; }
};

} // namespace gman

using GMANVector = gman::Vector<RtFloat>;

extern template class gman::Vector<RtFloat>;
extern template class gman::Vector<gman::PartnerReal>;
