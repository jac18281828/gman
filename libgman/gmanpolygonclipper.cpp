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
#include "gmanpolygonclipper.h" /* Declaration Header */
#include "gmanviewingsystem.h"


/*
 * RenderMan API GMANPolygonClipper
 *
 */

// default constructor
GMANPolygonClipper::GMANPolygonClipper() : UniversalSuperClass() { 
  GMANVector4 vec;

  pclip = &(clipper[GMANFRONT]);

  clipper[GMANFRONT].add(&(clipper[GMANBACK]));
  clipper[GMANBACK].add(&(clipper[GMANLEFT]));
  clipper[GMANLEFT].add(&(clipper[GMANRIGHT]));
  clipper[GMANRIGHT].add(&(clipper[GMANTOP]));
  clipper[GMANTOP].add(&(clipper[GMANBOTTOM]));
  clipper[GMANBOTTOM].add(NULL);

  // set plane normals
  vec = GMANVector4(0.0, 0.0, 1.0, 0.0);
  clipper[GMANFRONT].setNormal(vec.normalize());

  vec = GMANVector4(0.0, 0.0, -1.0, 0.0);
  clipper[GMANBACK].setNormal(vec.normalize());

  vec = GMANVector4(1.0, 0.0, 0.0, 0.0);
  clipper[GMANLEFT].setNormal(vec.normalize());

  vec = GMANVector4(-1.0, 0.0, 0.0, 0.0);
  clipper[GMANRIGHT].setNormal(vec.normalize());

  vec = GMANVector4(0.0, -1.0, 0.0, 0.0);
  clipper[GMANTOP].setNormal(vec.normalize());

  vec = GMANVector4(0.0, 1.0, 0.0, 0.0);
  clipper[GMANBOTTOM].setNormal(vec.normalize());

};


// default destructor 
GMANPolygonClipper::~GMANPolygonClipper() { };


int GMANPolygonClipper::clip(GMANFace *face, 
			     GMANOutputPolygon &out,
			     const GMANViewingSystem *vs) {
  const GMANVertex *vert;  // 3-D world space vertex
  GMANVertex4 hv;   // 4-D homogeneous coord vertex

  int nVerts = face->getNumVerts();
  for(int i=0; i < nVerts; i++) {
    // get world space vertex position pointer
    vert = face->getVertex(i);
    
    // set homogeneous coord
    hv.set(vert->getLocation(), 
	   vert->getColor(), 
	   vert->getAlpha(), 
	   vs->getProjMatrix());

    pclip->clip(hv, out);

  }
  pclip->close(out);

  return out.getNumVert();
}
