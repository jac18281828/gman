/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  February 2001  First release
  ---------------------------------------------------------
  Homogeneous coordinate point.
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

#include "gmanhpoint.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector4.h"
#include "ri.h"

namespace gman {

template <typename T> void HPoint<T>::wToOne() {
  w = 1.0 / w;
  this->x *= w;
  this->y *= w;
  this->z *= w;
  w = 1.0;
}

template <typename T> HPoint<T>& HPoint<T>::operator+=(HPoint const& p) {
  Point<T>(*this) += Point<T>(p);
  w += p.getW();

  return *this;
}

template <typename T> HPoint<T>& HPoint<T>::operator*=(T f) {
  Point<T>(*this) *= f;
  w *= f;
  return *this;
}

template <typename T> HPoint<T>& HPoint<T>::operator*=(Matrix4<T> const& m) {

  Vector4<T> vec(*this);

  vec *= m;
  HPoint res(vec.getX(), vec.getY(), vec.getZ(), vec.getW());
  *this = res;
  return *this;
}

template <typename T> HPoint<T> HPoint<T>::operator+(HPoint const& p) const {
  HPoint res(*this);
  res += p;
  return res;
}

template <typename T> HPoint<T> HPoint<T>::operator*(T f) const {
  HPoint res(*this);
  res *= f;
  return res;
}

template <typename T> HPoint<T> HPoint<T>::operator*(Matrix4<T> const& m) const {
  HPoint res(*this);

  res *= m;

  return res;
}

} // namespace gman

template class gman::HPoint<RtFloat>;
template class gman::HPoint<gman::PartnerReal>;
