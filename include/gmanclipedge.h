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
 

#ifndef __GMAN_GMANCLIPEDGE_H
#define __GMAN_GMANCLIPEDGE_H 1


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
#include "gmanoutputpolygon.h"
#include "gmanvector4.h"
#include "gmanvertex4.h"


/* Global types */
enum GMANPlane { GMANFRONT, GMANBACK, GMANLEFT, GMANRIGHT, GMANTOP, 
		 GMANBOTTOM };

/*
 * RenderMan API GMANClipEdge
 *
 */

class GMANDLL GMANClipEdge : public UniversalSuperClass {
private:
  GMANClipEdge *next;	// next clipper
  GMANVector4   normal;
  GMANVertex4 first;    // first vertex
  GMANVertex4 start;    // start vertex

  bool first_inside;	// first vertex inside flag
  bool start_inside;    // start vertex inside flag
  bool first_flag;	// first vertex seen flag

  // private methods
  bool isInside(const GMANVertex4 &v) {
    return (normal.dot(v.getCoord()) >= 0.0)?true:false;
  };
  
  GMANVertex4 intersect(const GMANVertex4 &s, const GMANVertex4 &e);
  
  RtVoid output(const GMANVertex4 &v, GMANOutputPolygon & out);
  
public:
  GMANClipEdge(); // default constructor

  ~GMANClipEdge(); // default destructor

  RtVoid add(GMANClipEdge *c) { next = c; };
  RtVoid clip(const GMANVertex4 &v, GMANOutputPolygon &p);
  RtVoid close(GMANOutputPolygon &p);
  RtVoid setNormal(const GMANVector4 &n) { normal = n; };
};


#endif

