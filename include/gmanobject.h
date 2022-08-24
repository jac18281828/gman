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
 

#ifndef __GMAN_GMANOBJECT_H
#define __GMAN_GMANOBJECT_H 1


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
// ray and segment type
#include "gmansegment.h"
#include "gmansurface.h"
#include "gmanface.h"
#include "gmanbody.h"
#include "gmanprimitive.h"

/*
 * RenderMan API GMANObject
 *
 * An object is composed of one or more bodies
 *
 */

class GMANDLL  GMANObject : public GMANPrimitive {
private:

  GMANBody *bodyRoot;	// body's

  GMANVertex *vertRoot; // verticies

  GMANObject *next;     // next instance

public:
  GMANObject();// default constructor

  GMANObject(GMANVertex *v, GMANBody *b); 

  ~GMANObject(); // default destructor


  // intersection test
  // return the surface that intersects the ray or NULL
  const GMANSurface *intersects(const GMANRay &ray);


  //
  // return a pointer to a handle for this object
  RtObjectHandle *getHandle(RtVoid);


  // clone ... must be able to clone a gman object
  // return an identical copy in a newly allocated memory
  // space
  GMANObject *clone(RtVoid);

  GMANObject *getNext(RtVoid) { return next; };
  RtVoid setNext(GMANObject *o) { next = o; };

  RtVoid setBody(GMANBody *b) { bodyRoot = b; };
  GMANBody *getBody(RtVoid) { return bodyRoot; };
  
  RtVoid setVert(GMANVertex *v) { vertRoot = v; };
  GMANVertex *getVert(RtVoid) { return vertRoot; };
};


#endif

