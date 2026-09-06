/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
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
  // LEFT/RIGHT/TOP/BOTTOM here are placeholders; clip() below rebuilds
  // them from the screen window on every call, for both projections.
  // FRONT/BACK (z, near/far) are correct for both and clip() never
  // touches them.
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

  // Both projections clip against RiScreenWindow: the same window
  // GMANViewingSystem::screenToRaster maps to the raster, so the bound
  // that discards geometry here and the bound that lays out pixels there
  // agree by construction. A window wider than the canonical +-1 square
  // must show more of the projected scene, not less.
  const GMANOptions::ScreenWindowStruct &sw = vs->getScreenWindow();
  clipper[GMANLEFT].setNormal(
      GMANVector4(1.0, 0.0, 0.0, -sw.left).normalize());
  clipper[GMANRIGHT].setNormal(
      GMANVector4(-1.0, 0.0, 0.0, sw.right).normalize());
  clipper[GMANTOP].setNormal(
      GMANVector4(0.0, -1.0, 0.0, sw.top).normalize());
  clipper[GMANBOTTOM].setNormal(
      GMANVector4(0.0, 1.0, 0.0, -sw.bottom).normalize());

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
