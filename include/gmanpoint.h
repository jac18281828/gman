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

#include "gman/export.h"
#include "gmanmatrix4.h"
#include "ri.h"

class GMANHPoint;

/*
 * RenderMan API GMANPoint
 *
 * A point in space
 *
 */

class GMAN_EXPORT GMANPoint {
public:
  // public type
  typedef enum { X = 0, Y = 1, Z = 2, NCOORDS } CoordType;

protected:
  // point spatial coordinates
  RtFloat x, y, z;

public:
  //! Constructor
  GMANPoint(RtFloat xv = 0.0, RtFloat yv = 0.0, RtFloat zv = 0.0) : x(xv), y(yv), z(zv) {}

  GMANPoint(RtPoint f) : x(f[X]), y(f[Y]), z(f[Z]) {}

  GMANPoint(GMANHPoint& p);

  //! Destructor
  ~GMANPoint() {}

  // X
  RtVoid setX(RtFloat xv) { x = xv; };
  RtFloat getX(RtVoid) const { return x; };

  // Y
  RtVoid setY(RtFloat yv) { y = yv; };
  RtFloat getY(RtVoid) const { return y; };

  // Z
  RtVoid setZ(RtFloat zv) { z = zv; };
  RtFloat getZ(RtVoid) const { return z; };

  const RtFloat& operator[](std::size_t i) const {
    if (i == X) {
      return x;
    }
    if (i == Y) {
      return y;
    }
    return z;
  }

  GMANPoint& operator=(RtFloat f) {
    x = f;
    y = f;
    z = f;
    return *this;
  }

  bool operator==(const GMANPoint& v) const;
  bool operator!=(const GMANPoint& v) const;

  bool operator<(const GMANPoint& p) const;

  // geometric addition of two points
  GMANPoint& operator+=(const GMANPoint& p) {
    x += p.x;
    y += p.y;
    z += p.z;

    return *this;
  }

  // scale point by a constant
  GMANPoint& operator*=(RtFloat f) {
    x *= f;
    y *= f;
    z *= f;

    return *this;
  }

  // geometric addition of two points
  GMANPoint operator+(const GMANPoint& p) const {
    GMANPoint res(*this);

    res += p;
    return res;
  }

  // scale point by a constant
  GMANPoint operator*(RtFloat f) const {
    GMANPoint res(*this);

    res *= f;
    return res;
  }

  // transform THIS point using matrix
  GMANPoint& operator*=(const GMANMatrix4& m);
};
