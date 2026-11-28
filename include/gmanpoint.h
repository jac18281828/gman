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

#include <cstddef>
#include <type_traits>

#include "gman/export.h"
#include "gmanmatrix4.h"
#include "gmanreal.h"
#include "ri.h"

namespace gman {

template <typename T> class GMAN_EXPORT HPoint;

} // namespace gman

using GMANHPoint = gman::HPoint<RtFloat>;

namespace gman {

/*
 * RenderMan API Point
 *
 * A point in space over the real scalar T. GMANPoint names the instance for
 * the RenderMan scalar. A pointer to any arithmetic type builds a Point, and
 * Point<U> converts explicitly to Point<T>.
 *
 */

template <typename T> class GMAN_EXPORT Point {
public:
  // public type
  typedef enum { X = 0, Y = 1, Z = 2, NCOORDS } CoordType;

protected:
  // point spatial coordinates
  T x, y, z;

public:
  //! Constructor
  Point(T xv = 0.0, T yv = 0.0, T zv = 0.0) : x(xv), y(yv), z(zv) {}

  template <typename U>
    requires std::is_arithmetic_v<U>
  explicit Point(U const* f) : x(static_cast<T>(f[X])), y(static_cast<T>(f[Y])), z(static_cast<T>(f[Z])) {}

  template <typename U>
  explicit Point(Point<U> const& p)
      : x(static_cast<T>(p.getX())), y(static_cast<T>(p.getY())), z(static_cast<T>(p.getZ())) {}

  Point(HPoint<T>& p);

  //! Destructor
  ~Point() {}

  // X
  void setX(T xv) { x = xv; };
  T getX() const { return x; };

  // Y
  void setY(T yv) { y = yv; };
  T getY() const { return y; };

  // Z
  void setZ(T zv) { z = zv; };
  T getZ() const { return z; };

  T const& operator[](std::size_t i) const {
    if (i == X) {
      return x;
    }
    if (i == Y) {
      return y;
    }
    return z;
  }

  Point& operator=(T f) {
    x = f;
    y = f;
    z = f;
    return *this;
  }

  bool operator==(Point const& v) const;
  bool operator!=(Point const& v) const;

  bool operator<(Point const& p) const;

  // geometric addition of two points
  Point& operator+=(Point const& p) {
    x += p.x;
    y += p.y;
    z += p.z;

    return *this;
  }

  // scale point by a constant
  Point& operator*=(T f) {
    x *= f;
    y *= f;
    z *= f;

    return *this;
  }

  // geometric addition of two points
  Point operator+(Point const& p) const {
    Point res(*this);

    res += p;
    return res;
  }

  // scale point by a constant
  Point operator*(T f) const {
    Point res(*this);

    res *= f;
    return res;
  }

  // transform THIS point using matrix
  Point& operator*=(Matrix4<T> const& m);
};

} // namespace gman

using GMANPoint = gman::Point<RtFloat>;

extern template class gman::Point<RtFloat>;
extern template class gman::Point<gman::PartnerReal>;
