/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns
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

#include <concepts>

#include <math.h>

#include "gman/export.h"
#include "gmanhpoint.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "ri.h"

/* forward declaration of the viewing system */
class GMANViewingSystem;

namespace gman {

/*
 * RenderMan API Vector4
 *
 * A four-component vector over the real scalar T; GMANVector4 names the RtFloat
 * instance, the only one built from a RenderMan array or projected by a
 * RenderMan matrix.
 */

template <typename T> class GMAN_EXPORT Vector4 : public Vector<T> {
private:
  T w; // the w-axis coord

  // cross product disabled for now

  // compute the cross product of this vector with another
  Vector4 cross(Vector<T> const& /*vec*/) { return *this; };

public:
  Vector4(); // default constructor

  Vector4(T xval, T yval, T zval, T wval) : Vector<T>(xval, yval, zval), w(wval) {};

  Vector4(RtFloat const* pts)
    requires std::same_as<T, RtFloat>
      : Vector<T>(pts) {
    // assume we get 4 pts
    w = pts[3];
  }

  Vector4(HPoint<T> const& p) : Vector<T>(p), w(p.getW()) {}

  template <typename U> explicit Vector4(Vector4<U> const& v) : Vector<T>(v), w(static_cast<T>(v.getW())) {}

  ~Vector4(); // default destructor

  /* Same as GMANVector: the destructor above deprecates the implicit copy
   * constructor, which gcc rejects under -Werror=deprecated-copy. Copy
   * assignment is user-provided below. */
  Vector4(Vector4 const&) = default;

  // get and set w
  T getW(RtVoid) const { return w; };
  RtVoid setW(T wval) { w = wval; };

  // get magnitude
  T magnitude(RtVoid) { return sqrt(this->x * this->x + this->y * this->y + this->z * this->z + w * w); };

  // mul/assign vector
  //
  // GMANVector(*this) *= GMANVector(v) would build a temporary GMANVector
  // slice, mutate that, and discard it, so x,y,z of *this would stay
  // unchanged and only w would change. Assign the components directly
  // instead.
  Vector4& operator*=(Vector4 const& v) {
    this->setX(this->getX() * v.getX());
    this->setY(this->getY() * v.getY());
    this->setZ(this->getZ() * v.getZ());
    w *= v.w;

    return *this;
  }

  // multiply vector by scaler
  Vector4& operator*=(T s) {
    this->setX(this->getX() * s);
    this->setY(this->getY() * s);
    this->setZ(this->getZ() * s);
    w *= s;
    return *this;
  }

  // add this vector and another
  Vector4 operator+(Vector4 const& v) const {
    Vector4 res(this->x + v.x, this->y + v.y, this->z + v.z, w + v.w);

    return res;
  };

  // sub this vector and another
  Vector4 operator-(Vector4 const& v) const {
    Vector4 res(this->x - v.x, this->y - v.y, this->z - v.z, w - v.w);

    return res;
  };

  // turn off some methods
  // assign a scaler to each vector value
  Vector4& operator=(T s) {
    this->setX(s);
    this->setY(s);
    this->setZ(s);
    w = s;
    return *this;
  };

  Vector4& operator=(Vector<T> const& v) {
    this->setX(v.getX());
    this->setY(v.getY());
    this->setZ(v.getZ());
    w = 1.0;
    return *this;
  }

  Vector4& operator=(Vector4 const& v) {
    this->setX(v.getX());
    this->setY(v.getY());
    this->setZ(v.getZ());
    w = v.w;
    return *this;
  }

  // subtract/assign vector
  Vector4& operator-=(Vector4 const& v) {
    this->setX(this->getX() - v.getX());
    this->setY(this->getY() - v.getY());
    this->setZ(this->getZ() - v.getZ());
    w -= v.w;
    return *this;
  }

  // divide vector by scaler
  Vector4& operator/=(T s) {
    this->setX(this->getX() / s);
    this->setY(this->getY() / s);
    this->setZ(this->getZ() / s);
    w /= s;
    return *this;
  }
  // multiply by a scaler
  Vector4 operator*(const T s) {
    Vector4 res(*this);
    res *= s;
    return res;
  };

  // divide by a scaler
  Vector4 operator/(const T s) {
    Vector4 res(*this);
    res /= s;
    return res;
  };

  Vector4& operator*=(Matrix4<T> const& m);

  operator HPoint<T>() {
    HPoint<T> res(this->getX(), this->getY(), this->getZ(), getW());

    return res;
  }

  // normalize vector by magnitude
  Vector4& normalize(RtVoid) {

    auto m = magnitude();

    if (m < RI_EPSILON)
      m = 1.0;

    *this /= m;

    return *this;
  }

  // compute the dot product of this vector with another
  T dot(Vector4 const& vec) {
    auto sDot = Vector<T>(*this).dot(Vector<T>(vec));
    return sDot + w * vec.w;
  }

  // premultiply point by projective matrix
  RtVoid projTransform(Point<T> const& p, RtMatrix const& ptm)
    requires std::same_as<T, RtFloat>
  {
    Vector<T> v(p);

    projTransform(v, ptm);
  }

  // premultiply vector by projective matrix
  RtVoid projTransform(Vector<T> const& p, RtMatrix const& ptm)
    requires std::same_as<T, RtFloat>
  {
    this->x = (RtFloat)(ptm[0][0] * p.getX() + ptm[0][1] * p.getY() + ptm[0][2] * p.getZ() + ptm[0][3]);

    this->y = (RtFloat)(ptm[1][0] * p.getX() + ptm[1][1] * p.getY() + ptm[1][2] * p.getZ() + ptm[1][3]);

    this->z = (RtFloat)(ptm[2][0] * p.getX() + ptm[2][1] * p.getY() + ptm[2][2] * p.getZ() + ptm[2][3]);

    w = (RtFloat)(ptm[3][0] * p.getX() + ptm[3][1] * p.getY() + ptm[3][2] * p.getZ() + ptm[3][3]);
  }

  // perform perspective division on point
  RtVoid perspective(Point<T>& p) {
    p.setX(this->x / w);
    p.setY(this->y / w);
    p.setZ(this->z / w);
  }

  // perform perspective division on vector
  RtVoid perspective(Vector<T>& v) const {
    v.setX(this->x / w);
    v.setY(this->y / w);
    v.setZ(this->z / w);
  }
};

} // namespace gman

using GMANVector4 = gman::Vector4<RtFloat>;

extern template class gman::Vector4<RtFloat>;
extern template class gman::Vector4<gman::PartnerReal>;
