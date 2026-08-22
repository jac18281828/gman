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
#include "gmanvertex.h" /* Declaration Header */
#include "gmanface.h"
#include "gmandefaults.h"


/*
 * RenderMan API GMANVertex
 *
 */

// default constructor
//
// alpha was never initialized here (unlike the other constructor, which
// defaults it to DefaultAlpha) -- harmless while nothing read a
// newly-tessellated vertex's alpha, but GMANClipEdge::intersect's color
// blend (GMANCombine, weighted by e.getAlpha()) reads it as soon as the
// clipper actually runs, turning the garbage into an out-of-range color
// that later corrupts gamma/quantization.
GMANVertex::GMANVertex() : location(0.0, 0.0, 0.0),
			   normal(0.0, 0.0, 0.0),
			   color(DefaultBGColor),
			   alpha(DefaultAlpha),
			   next(NULL),
			   faceList(NULL) { };


// default destructor 
GMANVertex::~GMANVertex() { };



// calculate vertex normal: the area-weighted average of the adjacent
// faces' geometric normals, for polygonal input with no analytic normal.
//
// No primitive in this tree populates faceList -- setFaceList has no
// caller anywhere -- so this fallback has no live call site today; the
// quadrics createParametric tessellates all supply an analytic normal
// from GMANParametric::getNormal instead (see createParametric). Written
// and correct for whichever future polygon tessellator populates
// adjacency, provided it also calls each face's calcArea() -- this
// assumes a nonzero weight, same as GMANFace's own default-constructed
// area of 0 assumes calcArea() runs before anything reads it. An empty or
// absent list is not an error, just nothing to average, so the default
// zero normal is left untouched.
RtVoid GMANVertex::calcNormal() {
  if (faceList == NULL || faceList->empty()) {
    return;
  }

  GMANVector accum(0.0, 0.0, 0.0);
  for (GMANFaceList::iterator face = faceList->begin();
       face != faceList->end();
       ++face) {
    accum += (*face)->getNormal() * (*face)->getArea();
  }
  accum.normalize();
  normal = accum;
};
