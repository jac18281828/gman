/* SPDX-License-Identifier: LGPL-2.1-or-later */

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
// logging
#include "gmanlog.h"

#include "gmanpoint.h"
#include "gmanvector.h"
#include "gmancolor.h"
#include "gmantypes.h"
#include "gmandefaults.h"

/*
 * RenderMan API GMANVertex
 *
 */

class GMAN_EXPORT  GMANVertex {
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
	     GMANFaceList &/*fl*/,
	     const GMANColor &/*col*/=DefaultBGColor,
	     const GMANAlpha &alp=DefaultAlpha) : location(position),
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

  // set the shading normal
  RtVoid setNormal(const GMANVector &n) { normal = n; };
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

