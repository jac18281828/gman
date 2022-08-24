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
 

#ifndef __GMAN_GMANSURFACE_H
#define __GMAN_GMANSURFACE_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// world feature characteristics
#include "gmanface.h"
#include "gmanvector.h"
#include "gmanvertex.h"


// forward declaration of body
class GMANBody;

/*
 * RenderMan API GMANSurface
 *
 * A surface is a list of primitive object types.
 *
 * A primitive is a face or quad.
 *
 */

class GMANDLL  GMANSurface : public UniversalSuperClass {
protected:
  GMANPoint		center;

  GMANBody		*parentBody;

  GMANFace		*faceRoot; // faces

  GMANSurface           *next;	  // next face pointer
  

public:
  // default constructor
  GMANSurface(GMANBody *p) {
    parentBody = p;
    faceRoot = NULL;
    next = NULL;
  };

  ~GMANSurface() { 
    GMANFace *faceEle = faceRoot;
    GMANFace *faceNex;

    while(faceEle != NULL) {
      faceNex = faceEle->getNext();
      delete  faceEle;
      faceEle = faceNex;
    }
  }; // default destructor

  // test all faces for intersection and return one if it does
  const GMANFace *intersects(const GMANRay &ray) {
    GMANFace *faceEle = faceRoot;
    while(faceEle != NULL) {
      // FIXME test face for intersection
      
      faceEle = faceEle->getNext();
    }
      
    return NULL;
  };

  // return unsent flux
  RtFloat getUnsentFlux(RtVoid) {
    /* FIXME: Should loop through faces and sum unsent flux
    return (color.getRed() + color.getGreen() +
	    color.getBlue()) * (RtFloat)area;
    */
    throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
  }

  // return face pointer
  GMANFace *getFace(RtVoid) { return faceRoot; };
  // set face pointer
  RtVoid setFace(GMANFace *face) { faceRoot = face; };
  
  // return next surface
  GMANSurface *getNext(RtVoid) { return next; }
  // set next surface
  RtVoid setNext(GMANSurface *surface) { next = surface; };

  // return the parent body
  GMANBody *getParent(RtVoid) { return parentBody; };

  const GMANPoint &getCenter(RtVoid) const { return center; };

  /*
  RtVoid calcCenter(RtVoid) {
    // initialize center
    GMANVector centerV = verticies[0]->getLocation();

    // find centroid
    for(int i=1; i<GMAN_NFACE_VERTS; i++) 
      centerV += verticies[i]->getLocation();

    centerV /= GMAN_NFACE_VERTS;

    center.setX(centerV.getX());
    center.setY(centerV.getY());
    center.setZ(centerV.getZ());

  }
  */
  
};


#endif

