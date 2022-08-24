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
 

#ifndef __GMAN_GMANFACE_H
#define __GMAN_GMANFACE_H 1


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
// the ray type
#include "gmansegment.h"
#include "gmantypes.h"
#include "gmanvertex.h"

/* Global Macros */

// quad surface (four verticies)
#define GMAN_NFACE_VERTS 4

// forward declaration of surface
class GMANSurface;

/*
 * RenderMan API GMANFace
 *
 */

class GMANDLL  GMANFace : public UniversalSuperClass {
protected:

  RtFloat	area;
  GMANSurface	*parentSurf;  // the parent surface
  GMANColor	color;    // the face color
  GMANVector    normal;   // normal to the face

  GMANVertex *verticies[GMAN_NFACE_VERTS]; // pointer array to vertexes

  GMANFace	*next;	  // next face pointer
  
public:
  GMANFace(GMANVertex *verts[GMAN_NFACE_VERTS], 
	   GMANSurface *p); // default constructor

  ~GMANFace(); // default destructor

  // Test this face for intersection in a ray
  bool intersects(const GMANRay	&ray) { 
    // fix me!
    return false; 
  };

  RtFloat getArea(RtVoid) { return area; }

  int getNumVerts(RtVoid) { return GMAN_NFACE_VERTS; };

  GMANSurface *getParent(RtVoid) { return parentSurf; };


  // set color
  RtVoid setColor(const GMANColor &c) { color = c; };
  // get color
  const GMANColor &getColor(RtVoid) const { return color; };
  // get normal
  const GMANVector &getNormal(RtVoid) const { return normal; };
  // get nth vertex
  const GMANVertex *getVertex(int n) const { return verticies[n]; };

  RtVoid calcArea(RtVoid);
  RtVoid calcNormal(RtVoid);

  // get next face
  GMANFace *getNext(RtVoid) { return next; };
  // set next face
  RtVoid setNext(GMANFace *face) { next = face; };
  
};


#endif
