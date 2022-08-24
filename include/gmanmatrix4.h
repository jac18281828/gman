/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  November 2000  First release

  April 2002:   JAC - major rewrite of this code.
  ---------------------------------------------------------
  4x4 matrix
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

#ifndef __GMANMATRIX4_H
#define __GMANMATRIX4_H 1

#include "ri.h"
#include "universalsuperclass.h"
#include "gmanmath.h"
#include "gmanerror.h"

class GMANVector;

class GMANDLL  GMANMatrix4 : public UniversalSuperClass
{
  private:
    RtMatrix mtrx;
  public:
    GMANMatrix4 ();
    GMANMatrix4 (RtMatrix m);
    
    RtVoid identity();
    RtVoid concat (const GMANMatrix4 &m);
    RtVoid persp (RtFloat fov);
    RtVoid trans (RtFloat dx, RtFloat dy, RtFloat dz);
    RtVoid rot (RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz);
    RtVoid scale (RtFloat sx, RtFloat sy, RtFloat sz);
    RtVoid skew (RtFloat angle, GMANVector &a, GMANVector &b);
    
    RtVoid prjPersp (RtFloat fov, RtFloat nearOne, RtFloat farOne);
    RtVoid prjOrtho (RtFloat nearOne, RtFloat farOne);
    
    RtFloat determinant (RtVoid);
    RtVoid invert (RtVoid);
    
    RtVoid p3m (RtInt n, RtFloat *src, RtFloat *dest);
    RtVoid p4m (RtInt n, RtFloat *src, RtFloat *dest);
    
    GMANMatrix4 operator*(RtFloat f) const ;
    GMANMatrix4 &operator*=(RtFloat f);
    GMANMatrix4 operator*(const GMANMatrix4 &m) const;
    GMANMatrix4 &operator*=(const GMANMatrix4 &m);
    GMANMatrix4 operator+(const GMANMatrix4 &m) const;
    GMANMatrix4 &operator+=(const GMANMatrix4 &m);

    GMANMatrix4 &operator=(const RtMatrix &m);
	GMANMatrix4 &operator=(const GMANMatrix4 &m);

	GMANMatrix4 &assign(const GMANMatrix4 &m);
    
	RtVoid setBasis(RtBasis &b);

    RtFloat  *operator[](size_t i) {
      return mtrx[i];
    }

    const RtFloat *operator[](size_t i) const {
	return mtrx[i];
    }

  const RtMatrix &get(RtVoid) const {
    return mtrx;
  }
};

#endif




