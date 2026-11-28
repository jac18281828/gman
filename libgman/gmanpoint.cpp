/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* LJL February 2001 */
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
#include "ri.h"

namespace gman {

template <typename T> Point<T>::Point(HPoint<T>& p) {
  p.wToOne();
  x = p.getX();
  y = p.getY();
  z = p.getZ();
}

/* operators */
//--------------------------------------------------
// == !=
template <typename T> bool Point<T>::operator==(Point const& v) const { return x == v.x && y == v.y && z == v.z; }
template <typename T> bool Point<T>::operator!=(Point const& v) const { return !(*this == v); }

template <typename T> bool Point<T>::operator<(Point const& p) const { return ((x < p.x) && (y < p.y) && (z < p.z)); }

//-------------------------------------------------------------
template <typename T> Point<T>& Point<T>::operator*=(Matrix4<T> const& m) {

  // transform the point
  const auto rx = m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3];
  const auto ry = m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3];
  const auto rz = m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3];

  x = rx;
  y = ry;
  z = rz;

  return *this;
}

} // namespace gman

template class gman::Point<RtFloat>;
template class gman::Point<gman::PartnerReal>;
