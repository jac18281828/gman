/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanobject.h" /* Declaration Header */
#include "gmanvertex.h"
#include "gmanbody.h"


/*
 * RenderMan API GMANObject
 *
 */

// default constructor
GMANObject::GMANObject() : 
  GMANPrimitive() { 
  vertRoot = NULL;
  bodyRoot = NULL;
  next     = NULL;
};

GMANObject::GMANObject(GMANVertex *v, GMANBody *b) : 
  GMANPrimitive() { 
  vertRoot = v;
  bodyRoot = b;
  next     = NULL;
};


// default destructor 
GMANObject::~GMANObject() { 
  GMANBody      *body = bodyRoot;
  GMANBody	*bNext;
  
  while(body != NULL) {
    bNext = body->getNext();
    delete body;
    body = bNext;
  };
  
  GMANVertex   *vert = vertRoot;
  GMANVertex   *vNext;
  while(vert != NULL) {
    vNext = vert->getNext();
    delete vert;
    vert = vNext;
  }
}

// intersection test
// return the surface that intersects the ray or NULL
// const GMANSurface *GMANObject::intersects(const GMANRay &ray)



GMANObject *GMANObject::clone(RtVoid) {
  // FIXME FIXME FIXME
  return NULL;
}
