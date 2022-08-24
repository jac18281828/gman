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
 

#ifndef __GMAN_GMANBODY_H
#define __GMAN_GMANBODY_H 1


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
// color
#include "gmancolor.h"

/*
 * RenderMan API GMANBody
 *
 * A body is a single complete spatial entity made
 * up of surfaces.
 *
 */

class GMANDLL GMANBody : public UniversalSuperClass {
  GMANColor	reflectance; // object reflectivity
  GMANColor     emittance;   // object emittivity
  GMANSurface   *surfaceRoot; // surfaces in body

  GMANBody	*next;	      // body list
  
public:
  GMANBody(const GMANColor &ref, const GMANColor &emit); // default constructor

  ~GMANBody(); // default destructor

  const  GMANColor &getReflectance(RtVoid) const { return reflectance; };

  const  GMANColor &getEmittance(RtVoid)  const { return emittance; };

  RtVoid setSurface(GMANSurface *surf) { surfaceRoot = surf; };
  GMANSurface *getSurface(RtVoid) { return surfaceRoot; }

  RtVoid setNext(GMANBody *n) { next = n; };
  GMANBody *getNext(RtVoid) { return next; };

};


#endif

