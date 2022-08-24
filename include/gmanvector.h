/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

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
 

#ifndef __GMAN_GMANVECTOR_H
#define __GMAN_GMANVECTOR_H 1


/* Headers */
#include "ri.h"
#include "universalsuperclass.h"
#include "gmantypes.h"
#include "gmanpoint.h"

/*
 * RenderMan API GMANVector
 *
 * A physical simulation vector, i.e., magnitude
 * and direction, not to be confused with the 
 * stl "vector" template
 *
 */

class GMANDLL  GMANVector : public UniversalSuperClass
{
  public:
    // public type
    typedef enum { X=0, Y=1, Z=2, NCOORDS } CoordType;
  protected:
    // point spatial coordinates
    RtVector	vec;

  public:
    GMANVector();
    GMANVector(RtFloat x, RtFloat y, RtFloat z);

	GMANVector(const RtFloat *pts) {
		for(int i=0; i<NCOORDS; i++) {
			vec[i] = pts[i];
		}
	}
    
    GMANVector(const GMANPoint &p);
    
    /* construct a vector from two points V = b - a */
    GMANVector(const GMANPoint &a, const GMANPoint &b);
    //dtor
    ~GMANVector();


    RtFloat    getX(RtVoid) const {
	return vec[X];
    }
    RtVoid    setX(RtFloat x) {
        vec[X] = x;
    }

    RtFloat    getY(RtVoid) const {
	return vec[Y];
    }
    RtVoid    setY(RtFloat y) {
	vec[Y] = y;
    }

    RtFloat    getZ(RtVoid) const {
	return vec[Z];
    }
    RtVoid    setZ(RtFloat z) {
	vec[Z] = z;
    }

    RtFloat   &operator[](size_t i) {
	return vec[i];
    }

    const RtFloat   &operator[](size_t i)  const {
	return vec[i];
    }

  operator GMANPoint() {
    GMANPoint res(getX(), getY(), getZ());
    
    return res;
  }

    GMANVector &operator=(const GMANPoint &p);
    
    RtFloat     magnitude(RtVoid);
    GMANVector &normalize(RtVoid);
    RtFloat     dot(const GMANVector &v) const;
    GMANVector  cross(const GMANVector &v) const;
    
    /* operators */
    GMANVector operator+(GMANVector const &v) const { 
	return GMANVector (vec[X]+v.vec[X], 
			   vec[Y]+v.vec[Y], 
			   vec[Z]+v.vec[Z]); 
    }
    
    GMANVector operator-(GMANVector const &v) const {
	return GMANVector (vec[X]-v.vec[X], vec[Y]-v.vec[Y], vec[Z]-v.vec[Z]); 
    }

    GMANVector operator-() const
    { return GMANVector (-vec[X], -vec[Y], -vec[Z]); }  
    
    GMANVector operator+(RtFloat f) const
    { return GMANVector (vec[X]+f, vec[Y]+f, vec[Z]+f); }
    GMANVector operator-(RtFloat f) const
    { return GMANVector (vec[X]-f, vec[Y]-f, vec[Z]-f); }
    GMANVector operator*(RtFloat f) const
    { return GMANVector (vec[X]*f, vec[Y]*f, vec[Z]*f); }
    GMANVector operator/(RtFloat f) const
    { return GMANVector (vec[X]/f, vec[Y]/f, vec[Z]/f); }
    
    GMANVector &operator=(RtFloat f)
    { vec[X]=vec[Y]=vec[Z]=f; return *this; }
    
    // asssign a GMANVector
    GMANVector &operator=(const GMANVector &v);
    
    GMANVector &operator+=(GMANVector const &v)
    { vec[X]+=v.vec[X]; vec[Y]+=v.vec[Y]; vec[Z]+=v.vec[Z]; return *this; }
    GMANVector &operator-=(GMANVector const &v)
    { vec[X]-=v.vec[X]; vec[Y]-=v.vec[Y]; vec[Z]-=v.vec[Z]; return *this; }
    GMANVector &operator*=(GMANVector const &v)
    { vec[X]*=v.vec[X]; vec[Y]*=v.vec[Y]; vec[Z]*=v.vec[Z]; return *this; }
    GMANVector &operator/=(GMANVector const &v)
    { vec[X]/=v.vec[X]; vec[Y]/=v.vec[Y]; vec[Z]/=v.vec[Z]; return *this; }
    
    GMANVector &operator+=(RtFloat f)
    { vec[X]+=f; vec[Y]+=f; vec[Z]+=f; return *this; }
    GMANVector &operator-=(RtFloat f)
    { vec[X]-=f; vec[Y]-=f; vec[Z]-=f; return *this; }
    GMANVector &operator*=(RtFloat f)
    { vec[X]*=f; vec[Y]*=f; vec[Z]*=f; return *this; }
    GMANVector &operator/=(RtFloat f)
    { vec[X]/=f; vec[Y]/=f; vec[Z]/=f; return *this; }
    
    GMANVector operator*(const GMANMatrix4 &m) const;
    GMANVector &operator*=(const GMANMatrix4 &m);

  bool operator<(const GMANVector &v) const {
    for(int i=0; i<NCOORDS; i++) {
      if(vec[i] < v.vec[i])
	return true;
    }
    return false;
  }
};

#endif
