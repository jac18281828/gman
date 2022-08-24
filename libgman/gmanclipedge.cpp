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

/* system headers */
#include <math.h>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanclipedge.h" /* Declaration Header */


/*
 * RenderMan API GMANClipEdge
 *
 */

// default constructor
GMANClipEdge::GMANClipEdge() : UniversalSuperClass() { 
  first_flag=false;
};


// default destructor 
GMANClipEdge::~GMANClipEdge() { };


RtVoid GMANClipEdge::output(const GMANVertex4 &v, GMANOutputPolygon &out) {
  
  if(next != NULL) 
    next->clip(v, out);
  else
    out.addVertex(v);

}

GMANVertex4 GMANClipEdge::intersect(const GMANVertex4 &s, 
				    const GMANVertex4 &e) {

  RtFloat	d, t;
  GMANColor	color;
  GMANVector4   p, r;
  GMANVertex4   v;


  r = e.getCoord() - s.getCoord();
  d = normal.dot(r);

  if(fabs(d) > RI_EPSILON)
    t = -normal.dot(s.getCoord())/d;
  else
    t = 1.0;

  if(t < 0.0)
    t = 0.0;

  if(t > 1.0)
    t = 1.0;

  // linearly interpolate vertex color
  GMANCombine colorCombine;
  color = colorCombine(s.getColor(), e.getColor(), e.getAlpha());

  GMANAlphaCombine alphaCombine;

  GMANAlpha alpha = alphaCombine(s.getAlpha(), e.getAlpha(), e.getAlpha());
  v.set(p, color, alpha);

  return v;

}


RtVoid GMANClipEdge::clip(const GMANVertex4 &current, GMANOutputPolygon &out) {
  bool curr_inside;   // current point inside flag

  GMANVertex4 isect;  // intersection vertex

  // check visibility
  curr_inside = isInside(current);

  if(first_flag == false) {
    first = current;
    first_inside = curr_inside;
    first_flag = true;
  } else {
    // does edge intersect plane?
    if(start_inside ^ curr_inside)
      {
	isect = intersect(start, current);
	output(current, out);
      }
  }
  if(curr_inside)
    output(current, out);

  start = current;
  start_inside = curr_inside;
}


RtVoid GMANClipEdge::close(GMANOutputPolygon &out) {
  GMANVertex4 isect; // intersection vertex

  if(first_flag) {
    // does edge intersect plane
    if(start_inside ^ first_inside) {
      isect = intersect(start, first);
      output(isect, out);
    }

    if(next != NULL) // more planes
      next->close(out);

    // reset first vertex seen flag
    first_flag = false;
  }
}
