/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* added Vector * Matrix -- LJL */

/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.
  

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.
  
  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/

/* system headers */
#include <math.h>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanpoint.h" /* Super class */
#include "gmanvector.h" /* Declaration Header */


/*
 * RenderMan API GMANVector
 *
 */
GMANVector::GMANVector() {
    for(RtInt i=0; i<NCOORDS; i++) {
	vec[i] = 0.0;
    }
}

GMANVector::GMANVector(RtFloat x, RtFloat y, RtFloat z) {
    setX(x);
    setY(y);
    setZ(z);
}

GMANVector::GMANVector(const GMANPoint &p) {

    *this = p;
}

/* construct a vector from two points V = b - a */
GMANVector::GMANVector(const GMANPoint &a, const GMANPoint &b)
{
  setX(b.getX() - a.getX());
  setY(b.getY() - a.getY());
  setZ(b.getZ() - a.getZ());
}

GMANVector::~GMANVector() {
  // not much to do
}

// copy operator
GMANVector &GMANVector::operator=(const GMANPoint &p) {
    setX(p.getX());
    setY(p.getY());
    setZ(p.getZ());
    
    return *this;
}

// copy operator
GMANVector &GMANVector::operator=(const GMANVector &v) {
    setX(v.getX());
    setY(v.getY());
    setZ(v.getZ());

    return *this;
}

RtFloat     GMANVector::magnitude(RtVoid)
{
    return sqrt(vec[X]*vec[X] + vec[Y]*vec[Y] + vec[Z]*vec[Z]);
}

GMANVector &GMANVector::normalize(RtVoid)
{
    RtFloat mag = magnitude();
    if(mag < RI_EPSILON) {
	return *this;
    }
    *this /= mag;
    return *this;
}

RtFloat     GMANVector::dot(const GMANVector &v) const
{
    RtFloat rc=0.0;
    for(RtInt i=0; i<NCOORDS; i++) {
	rc += vec[i]*v.vec[i];
    }
    return rc;
}

GMANVector  GMANVector::cross(const GMANVector &v) const
{
    GMANVector result( vec[Y]*v.vec[Z] - vec[Z]*v.vec[Y],
		       vec[Z]*v.vec[X] - vec[X]*v.vec[Z],
		       vec[X]*v.vec[Y] - vec[Y]*v.vec[X] );
    return result;
}


/* operators */
GMANVector GMANVector::operator *(const GMANMatrix4 &m) const {
    GMANVector res(*this);

    res *= m;

    return res;
}

GMANVector &GMANVector::operator *=(const GMANMatrix4 &m) {

    // transform the point 
    GMANVector res;
    
    for(RtInt i=0; i<NCOORDS; i++) {
	res[i] = m[i][0]*vec[X] +
	    m[i][1]*vec[Y] +
	    m[i][2]*vec[Z] +
	    m[i][3];
    }
    
    *this = res;
    return *this;
}
