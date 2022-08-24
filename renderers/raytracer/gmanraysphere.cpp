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

#include "gmanraysphere.h"
#include "gmanvector.h"

bool GMANRaySphere::intersect(const GMANRay& ray, RtFloat &t ) const
{
  GMANVector direction( ray.getP1(), ray.getP2() );
//  RtPoint intersection;
  GMANPoint centre( 0, 0, 0);
  RtFloat coef[3];
  int numRoots;
  RtFloat roots[2];



  GMANVector deltaP( ray.getP1(), centre);
  
  coef[0] = direction.dot( direction);
  coef[1] = 2*direction.dot( deltaP);
  coef[2] = deltaP.dot( deltaP) - radius*radius;
  
  //   QuadraticRoots(coef, numRoots, roots);
  if (numRoots == 0) return false;
  
  if (numRoots==1)
    t = roots[0];
  else {
    if (roots[0]>0 && (roots[1]<0 || roots[0]<roots[1]) ) {
      t = roots[0];
    }
    else {
      t = roots[1];
    }
  }

  if ( t < RI_EPSILON ) return false;
  /*
  if (calcData ){
    intersection = pvAdd( ray.getP1(),
			  svMpy( t, currentRay.direction) );
    hit.t = t;
    hit.point = intersection;
    hit.normal = ppSub(intersection, sphere.centre);
    hit.side = -1;
    }*/
  return true;
}
