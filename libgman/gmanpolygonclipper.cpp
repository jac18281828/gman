/* SPDX-License-Identifier: LGPL-2.1-or-later */

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
#include "gmanlog.h"
#include "gmanpolygonclipper.h" /* Declaration Header */
#include "gmanviewingsystem.h"


/*
 * RenderMan API GMANPolygonClipper
 *
 */

// default constructor
GMANPolygonClipper::GMANPolygonClipper() { 
  GMANVector4 vec;

  pclip = &(clipper[GMANFRONT]);

  clipper[GMANFRONT].add(&(clipper[GMANBACK]));
  clipper[GMANBACK].add(&(clipper[GMANLEFT]));
  clipper[GMANLEFT].add(&(clipper[GMANRIGHT]));
  clipper[GMANRIGHT].add(&(clipper[GMANTOP]));
  clipper[GMANTOP].add(&(clipper[GMANBOTTOM]));
  clipper[GMANBOTTOM].add(NULL);

  // Set plane normals. GMANClipEdge::isInside tests normal.dot(v) >= 0
  // against the vertex's raw (un-divided) homogeneous coordinate, so
  // these are the six w-relative half-spaces of the canonical clip
  // volume (-w<=x<=w, -w<=y<=w, -w<=z<=w under the GL-style z mapping
  // GMANMatrix4::prjPersp/prjOrtho produce) -- each needs a w component
  // of 1, not the 3-vector frustum direction alone, or the "plane" is
  // really just the coordinate-space halved at 0, clipping away anything
  // with that coordinate negative regardless of w.
  //
  // LEFT/RIGHT/TOP/BOTTOM here are placeholders good only for prjPersp;
  // clip() below rebuilds them from the screen window on every call when
  // the active viewing system is orthographic. FRONT/BACK (z, near/far)
  // are correct for both and clip() never touches them.
  vec = GMANVector4(0.0, 0.0, 1.0, 1.0);
  clipper[GMANFRONT].setNormal(vec.normalize());

  vec = GMANVector4(0.0, 0.0, -1.0, 1.0);
  clipper[GMANBACK].setNormal(vec.normalize());

  vec = GMANVector4(1.0, 0.0, 0.0, 1.0);
  clipper[GMANLEFT].setNormal(vec.normalize());

  vec = GMANVector4(-1.0, 0.0, 0.0, 1.0);
  clipper[GMANRIGHT].setNormal(vec.normalize());

  vec = GMANVector4(0.0, -1.0, 0.0, 1.0);
  clipper[GMANTOP].setNormal(vec.normalize());

  vec = GMANVector4(0.0, 1.0, 0.0, 1.0);
  clipper[GMANBOTTOM].setNormal(vec.normalize());

};


// default destructor 
GMANPolygonClipper::~GMANPolygonClipper() { };


int GMANPolygonClipper::clip(GMANFace *face,
			     GMANOutputPolygon &out,
			     const GMANViewingSystem *vs) {
  const GMANVertex *vert;  // 3-D world space vertex
  GMANVertex4 hv;   // 4-D homogeneous coord vertex

  // GMANMatrix4::prjPersp always sets mtrx[3][3]=0.0 so w carries z;
  // prjOrtho leaves row 3 at GMANMatrix4::identity()'s default (w
  // constant 1), never having a reason to touch it. That is the one bit
  // distinguishing them here, and exactly the property that decides
  // whether the fixed +-1 LEFT/RIGHT/TOP/BOTTOM planes below mean
  // anything: under prjPersp they are invT's own calibration of the fov
  // cone (the correct NDC test, unrelated to RiScreenWindow -- see
  // GMANViewingSystem::screenToRaster, the one place both projections
  // apply the screen window, once). prjOrtho has no fov-equivalent
  // natural bound to calibrate against, so for an orthographic vs these
  // four planes are rebuilt from the screen window directly instead --
  // otherwise an orthographic camera clips at a hardcoded camera-space
  // unit box regardless of what RiScreenWindow actually asked for.
  if (vs->getProjMatrix()[3][3] != 0.0) {
    const GMANOptions::ScreenWindowStruct &sw = vs->getScreenWindow();
    clipper[GMANLEFT].setNormal(
        GMANVector4(1.0, 0.0, 0.0, -sw.left).normalize());
    clipper[GMANRIGHT].setNormal(
        GMANVector4(-1.0, 0.0, 0.0, sw.right).normalize());
    clipper[GMANTOP].setNormal(
        GMANVector4(0.0, -1.0, 0.0, sw.top).normalize());
    clipper[GMANBOTTOM].setNormal(
        GMANVector4(0.0, 1.0, 0.0, -sw.bottom).normalize());
  } else {
    clipper[GMANLEFT].setNormal(GMANVector4(1.0, 0.0, 0.0, 1.0).normalize());
    clipper[GMANRIGHT].setNormal(GMANVector4(-1.0, 0.0, 0.0, 1.0).normalize());
    clipper[GMANTOP].setNormal(GMANVector4(0.0, -1.0, 0.0, 1.0).normalize());
    clipper[GMANBOTTOM].setNormal(GMANVector4(0.0, 1.0, 0.0, 1.0).normalize());
  }

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
