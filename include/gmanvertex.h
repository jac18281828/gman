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
 

#ifndef __GMAN_GMANVERTEX_H
#define __GMAN_GMANVERTEX_H 1


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

#include "gmanpoint.h"
#include "gmanvector.h"
#include "gmancolor.h"
#include "gmantypes.h"
#include "gmandefaults.h"

/*
 * RenderMan API GMANVertex
 *
 */

class GMANDLL  GMANVertex : public UniversalSuperClass {
private:
  GMANPoint		location;

  GMANVector		normal;

  GMANColor		color;
  
  GMANAlpha		alpha;

  GMANVertex		*next; // next vertex

  GMANFaceList		*faceList;
public:
  // default constructor
  GMANVertex();

  GMANVertex(const GMANPoint &position, 
	     GMANFaceList &fl,
	     const GMANColor &col=DefaultBGColor,
	     const GMANAlpha &alp=DefaultAlpha) : 
    UniversalSuperClass(),
    location(position),
    normal(0.0, 0.0, 0.0),
    alpha(alp),
    next(NULL),
    faceList(NULL)
  { };
    

  ~GMANVertex(); // default destructor


  // set location
  RtVoid setLocation(const GMANPoint &p) { location = p; };
  // get location
  const GMANPoint &getLocation(RtVoid) const { return location; };

  // set color
  RtVoid setColor(const GMANColor &c) { color = c; };
  // get color
  const GMANColor &getColor(RtVoid) const { return color; };


  // set alpha
  RtVoid setColor(const GMANAlpha &a) { alpha = a; };
  // get alpha
  const GMANAlpha &getAlpha(RtVoid) const { return alpha; };

  // return the normal
  const GMANVector &getNormal(RtVoid) const { return normal; };
  // set next vertex
  RtVoid setNext(GMANVertex *n) { next = n; };
  // return next vertex
  GMANVertex *getNext(RtVoid) { return next; };

  // calculate the vertex normal
  RtVoid calcNormal(RtVoid);

  // set the face list
  RtVoid setFaceList(GMANFaceList *fl) { faceList = fl; }
  // return the face list
  GMANFaceList *getFaceList(RtVoid) const { return faceList; }
  
};


#endif

