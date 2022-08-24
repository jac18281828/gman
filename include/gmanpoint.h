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
 

#ifndef __GMAN_GMANPOINT_H
#define __GMAN_GMANPOINT_H 1

/* Headers */
#include "ri.h"
#include "universalsuperclass.h"
#include "gmanmatrix4.h"

class GMANHPoint;


/*
 * RenderMan API GMANPoint
 *
 * A point in space
 *
 */

class GMANDLL  GMANPoint : public UniversalSuperClass
{
public:
    // public type
    typedef enum { X=0, Y=1, Z=2, NCOORDS } CoordType;
protected:
	// point spatial coordinates
    RtPoint	c;

 public:
    //! Constructor
  GMANPoint(RtFloat xv = 0.0, RtFloat yv = 0.0, RtFloat zv = 0.0) {
   c[X] = xv;
   c[Y] = yv;
   c[Z] = zv;
  }
	
    GMANPoint(RtPoint f) {
      for(RtInt i=0; i<NCOORDS; i++) {
	c[i] = f[i];
      }
    }

    GMANPoint(GMANHPoint &p);
    
    //! Destructor
    ~GMANPoint() {}

    // X
    RtVoid setX(RtFloat xv) { c[X] = xv; };
    RtFloat getX(RtVoid) const { return c[X]; };

    // Y
    RtVoid setY(RtFloat yv) { c[Y] = yv; };
    RtFloat getY(RtVoid) const { return c[Y]; };
    
    // Z
    RtVoid setZ(RtFloat zv) { c[Z] = zv; };
    RtFloat getZ(RtVoid) const { return c[Z]; };
    
    GMANPoint &operator=(RtFloat f) { 
	for(RtInt i=0; i<NCOORDS; i++) {
	    c[i] = f;
	}
	return *this;
    }

    bool operator==(const GMANPoint &v) const;
    bool operator!=(const GMANPoint &v) const;

	bool GMANPoint::operator<(const GMANPoint &p) const;

	// geometric addition of two points
	GMANPoint &operator +=(const GMANPoint &p) {
		for(int i=0; i<NCOORDS; i++) {
			c[i] += p.c[i];
		}

		return *this;
	}

	// scale point by a constant
	GMANPoint &operator *=(RtFloat f) {
		for(int i=0; i<NCOORDS; i++) {
			c[i] *= f;
		}

		return *this;
	}

	// geometric addition of two points
	GMANPoint operator +(const GMANPoint &p) const {
		GMANPoint res(*this);

		res += p;
		return res;
	}

	// scale point by a constant
	GMANPoint operator *(RtFloat f) const {
		GMANPoint res(*this);

		res *= f;
		return res;
	}


    // transform THIS point using matrix
    GMANPoint &operator *=(const GMANMatrix4 &m);

    // transform point using matrix and return it
    // do not modify this point

    GMANPoint operator *(const GMANMatrix4 &m) const;


  const RtPoint &get(RtVoid) const {
    return c;
  }

};


#endif // __GMAN_GMANPOINT_H
