/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
 */

/* added Vector * Matrix -- LJL */

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

#include <math.h>

#include "gmanmatrix4.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

/* operators */
template <typename T> Vector<T>& Vector<T>::operator*=(Matrix4<T> const& m) {

  // transform the point
  Vector res;

  res.x = m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3];
  res.y = m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3];
  res.z = m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3];

  *this = res;
  return *this;
}

} // namespace gman

template class gman::Vector<RtFloat>;
template class gman::Vector<gman::PartnerReal>;
