/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanvector.h" /* Super class */
#include "gmanvector4.h" /* Declaration Header */


/*
 * RenderMan API GMANVector4
 *
 */

// default constructor
GMANVector4::GMANVector4() : GMANVector() { };


// default destructor 
GMANVector4::~GMANVector4() { };


GMANVector4 &GMANVector4::operator*=(const GMANMatrix4 &m) {
	// Row-vector convention (p*M, AGENTS.md's "Matrix convention"):
	// result[j] = sum_i vec[i]*m[i][j] over all four components -- the
	// same formula GMANMatrix4::p4m implements. GMANVector::operator*=
	// is the opposite convention (column-vector, implicit w=1, no real w
	// out) and would silently leave w stale; a real 4-component multiply
	// belongs here, not delegated.
	RtFloat x = getX(), y = getY(), z = getZ(), ww = w;

	setX(x * m[0][0] + y * m[1][0] + z * m[2][0] + ww * m[3][0]);
	setY(x * m[0][1] + y * m[1][1] + z * m[2][1] + ww * m[3][1]);
	setZ(x * m[0][2] + y * m[1][2] + z * m[2][2] + ww * m[3][2]);
	w    = x * m[0][3] + y * m[1][3] + z * m[2][3] + ww * m[3][3];

	return *this;
}

