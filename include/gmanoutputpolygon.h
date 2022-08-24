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
 

#ifndef __GMAN_GMANOUTPUTPOLYGON_H
#define __GMAN_GMANOUTPUTPOLYGON_H 1

/**
 ** This code based on source presented in Radiosity, A Programmer's 
 ** Perspective by Ian Ashdown.
 **/

/* Headers */

// STL
#include <vector>
#if HAVE_STD_NAMESPACE
using std::vector;
#endif

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
#include "gmanpoint.h"
#include "gmancolor.h"
#include "gmanvertex.h"
#include "gmanvertex4.h"

/*
 * RenderMan API GMANOutputPolygon
 *
 */

class GMANDLL  GMANOutVertex : public UniversalSuperClass // Output Vertex
{ 
  private:
    GMANColor color;
    GMANPoint posn;

  public:
    bool operator<(const GMANOutVertex &vert) const {
      GMANVector posVec(posn);
      GMANVector vertPos(vert.posn);
	return((color < vert.color) &&
	       (posVec < vertPos));
    }


    const GMANPoint &getPosn(RtVoid) const { return posn; }
    const GMANColor &getColor(RtVoid) const { return color; }

    RtVoid set(const GMANVertex4 &v) {
	GMANVector4 c = v.getCoord();
	c.perspective(posn);
	color = v.getColor();
    }
};


class GMANDLL  GMANOutputPolygon : public UniversalSuperClass {
private:

  vector<GMANOutVertex> vertexVec; // output array

  
public:
  GMANOutputPolygon(); // default constructor

  ~GMANOutputPolygon(); // default destructor

  int getNumVert(RtVoid) { return vertexVec.size(); };

  const GMANPoint& getVertexPosn(int n) {
    return vertexVec[n].getPosn();
  };

  const GMANColor &getVertexColor(int n) {
    return vertexVec[n].getColor();
  };

  RtVoid addVertex(const GMANVertex4 &v) {
	  GMANOutVertex  vert;

    vert.set(v);
    
    vertexVec.push_back(vert);

  };

  RtVoid reset(RtVoid) { vertexVec.clear(); } ;

};


#endif

