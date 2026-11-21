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
#include "gmanpoint.h"
#include "gmanvector.h"
#include "ri.h"

/* operators */
GMANVector& GMANVector::operator*=(const GMANMatrix4& m) {

  // transform the point
  GMANVector res;

  for (RtInt i = 0; i < NCOORDS; i++) {
    res[i] = m[i][0] * vec[X] + m[i][1] * vec[Y] + m[i][2] * vec[Z] + m[i][3];
  }

  *this = res;
  return *this;
}
