/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns 
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
 

#ifndef __GMAN_GMANVECTOR4_H
#define __GMAN_GMANVECTOR4_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>
#include <math.h>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// Our parent class
#include "gmanvector.h"
#include "gmanhpoint.h"
#include "gmantypes.h"

/* forward declaration of the viewing system */
class GMANViewingSystem;
   
/*
 * RenderMan API GMANVector4
 *
 */

class GMANDLL  GMANVector4 : public GMANVector {
private:
  RtFloat	w;  // the w-axis coord


  // cross product disabled for now

  // compute the cross product of this vector with another
  GMANVector4 cross(const GMANVector &vec) { return *this; }; 


public:
  GMANVector4(); // default constructor

  GMANVector4(RtFloat xval, RtFloat yval, RtFloat zval, RtFloat wval) :
    GMANVector(xval, yval, zval), w(wval) { };

	GMANVector4(RtFloat *pts) : GMANVector(pts) {
		//assume we get 4 pts
		w = pts[3];
	}

	GMANVector4(const GMANHPoint &p) :
		GMANVector(p), w(p.getW()) {
	}

  ~GMANVector4(); // default destructor

  // get and set w
  RtFloat getW(RtVoid) const { return w; };
  RtVoid setW(RtFloat wval) { w = wval; };

  // get magnitude
  RtFloat magnitude(RtVoid) { 
    return sqrt(vec[X]*vec[X] + vec[Y]*vec[Y] + vec[Z]*vec[Z] + w*w);
  };

  // mul/assign vector
  GMANVector4 &operator*=(const GMANVector4 &v) {
	  GMANVector(*this)*=GMANVector(v);
	  w *= v.w;

    return *this;
  }

  // multiply vector by scaler
  GMANVector4 &operator*=(RtFloat s) {
	  GMANVector(*this)*=GMANVector(s);
	  w *= s;
    return *this;
  }


  // add this vector and another
  GMANVector4 operator+(const GMANVector4 &v) const {
    GMANVector4 res(vec[X] + v.vec[X],
		    vec[Y] + v.vec[Y],
		    vec[Z] + v.vec[Z],
		    w + v.w);

    return res;
  };

  // sub this vector and another
  GMANVector4 operator-(const GMANVector4 &v) const {
    GMANVector4 res(vec[X] - v.vec[X],
		    vec[Y] - v.vec[Y],
		    vec[Z] - v.vec[Z],
		    w - v.w);

    return res;
  };

  // turn off some methods
  // assign a scaler to each vector value
  GMANVector4 &operator=(RtFloat s) {
	  GMANVector(*this) = s;
	  w=s;
    return *this;
  };

  GMANVector4 &operator=(const GMANVector &v) {
	  GMANVector(*this)=v;
	  w=1.0;
	  return *this;
  }

  GMANVector4 &operator=(const GMANVector4 &v) {
	  GMANVector(*this)=GMANVector(v);
	  w=v.w;
	  return *this;
  }


  // subtract/assign vector
  GMANVector4 &operator-=(const GMANVector4 &v) {
	  GMANVector(*this)-=GMANVector(v);
	  w -= v.w;
    return *this;
  }

  // divide vector by scaler
  GMANVector4 &operator/=(RtFloat s) {
	  GMANVector(*this)/=s;
	  w/=s;
    return *this;
  }
  // multiply by a scaler
  GMANVector4 operator*(const RtFloat s) {
	  GMANVector4 res(*this);
	  res *= s;
    return res;
  };

  // divide by a scaler
  GMANVector4 operator/(const RtFloat s) {
	  GMANVector4 res(*this);
	  res /= s;
    return res;
  };

  GMANVector4 &operator*=(const GMANMatrix4 &m);

  operator GMANHPoint() {
	  GMANHPoint res(getX(), getY(), getZ(), getW());

	  return res;
  }

  // normalize vector by magnitude
  GMANVector4 &normalize(RtVoid) {
    
    RtFloat m = magnitude();

    if(m < RI_EPSILON)
      m = 1.0;

    *this /= m;

    return *this;
  }

  // compute the dot product of this vector with another
  RtFloat dot(const GMANVector4 &vec) { 
	  RtFloat sDot=GMANVector(*this).dot(GMANVector(vec));
    return sDot + w*vec.w;
  }


  // premultiply point by projective matrix
  RtVoid projTransform(const GMANPoint &p, RtFloat (*ptm)[4]) {
    GMANVector v(p);

    projTransform(v, ptm);
  }


  // premultiply vector by projective matrix
  RtVoid projTransform(const GMANVector &p, const RtMatrix &ptm) {
    vec[X] = (RtFloat)(ptm[0][0]*p.getX() + 
			ptm[0][1]*p.getY() +
			ptm[0][2]*p.getZ() +
			ptm[0][3]);

    vec[Y] = (RtFloat)(ptm[1][0]*p.getX() + 
			ptm[1][1]*p.getY() +
			ptm[1][2]*p.getZ() +
			ptm[1][3]);

    vec[Z] = (RtFloat)(ptm[2][0]*p.getX() + 
			ptm[2][1]*p.getY() +
			ptm[2][2]*p.getZ() +
			ptm[2][3]);

    w = (RtFloat)(ptm[3][0]*p.getX() + 
		  ptm[3][1]*p.getY() +
		  ptm[3][2]*p.getZ() +
		  ptm[3][3]);

  }

  // perform perspective division on point
  RtVoid perspective(GMANPoint &p) {
    p.setX(vec[X]/w);
    p.setY(vec[Y]/w);
    p.setZ(vec[Z]/w);
  }

  // perform perspective division on vector
  RtVoid perspective(GMANVector &v) const {
    v.setX(vec[X]/w);
    v.setY(vec[Y]/w);
    v.setZ(vec[Z]/w);
  }

};


#endif

