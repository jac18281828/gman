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

#include <cmath>
#include <typeinfo>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanmath.h"
#include "gmanrenderer.h" /* Super class */
#include "gmanzbufferrenderer.h" /* Declaration Header */


/*
 * RenderMan API GMANZBufferRenderer
 *
 */

// default constructor
GMANZBufferRenderer::GMANZBufferRenderer(int w,
					 int h) :
  GMANRenderer(), width(w), height(h) {
  zbuffer=NULL;
  edge_list=NULL;
  viewingSys=NULL;
};
GMANZBufferRenderer::GMANZBufferRenderer() :
  GMANRenderer(), width(0), height(0) {
  zbuffer=NULL;
  edge_list=NULL;
  viewingSys=NULL;
};


// default destructor
GMANZBufferRenderer::~GMANZBufferRenderer() { 
  if(zbuffer) delete []zbuffer;
  if(edge_list) delete []edge_list;
};


/* 
 * Initialize the Z-buffer.
 */
bool GMANZBufferRenderer::initZBuffer(void) {

  if((width > 0) && (height>0)) {

    if(zbuffer) delete []zbuffer;

    zbuffer = new RtFloat[width*height];

    if(zbuffer) {
      for(int x=0; x<width; x++) {
	for(int y=0; y<height; y++) {
	  setZBuffer(x, y, RI_INFINITY);
	}
      }

      if(edge_list) delete []edge_list;
      // Value-initialized: buildEdgeList sets nisect over [ymin,ymax] and
      // drawEdgeList reads no further, but zeroing here makes an
      // indeterminate EdgeInfo unreachable by construction rather than by
      // that argument holding.
      edge_list = new EdgeInfo[height]();
      if(edge_list == NULL) 
	return false;

      return true;
    }
  }
  return false;
}


// render each clipped output polygon
void GMANZBufferRenderer::render(GMANOutputPolygon &out,
				 GMANFrameBuffer *frameBuffer)
 {

  if (!getVertexInfo(out)) {
    return;
  }
  scanEdges();
  drawEdgeList(frameBuffer);

};


// Get vertex info
bool GMANZBufferRenderer::getVertexInfo(GMANOutputPolygon &out) {

  VertexInfo    *vert;
  GMANPoint	posn;

  // y axis
  ymax = 0;
  ymin = height - 1;

  num_vert = out.getNumVert();
  if (num_vert > kMaxClippedVerts) {
    warning("Clipped polygon has %d vertices, more than the %d this "
            "renderer expects from a quad; dropping the rest.",
            num_vert, kMaxClippedVerts);
    num_vert = kMaxClippedVerts;
  }

  // A raster coordinate this far outside the frame can only come from a
  // near-zero (but not clipped) w: the near plane defaults to RI_EPSILON,
  // so "in front of the camera" and "safely away from a divide blowup"
  // are not the same test, and a face straddling the near plane can put
  // an intersection vertex arbitrarily close to it. A legitimately
  // partially-offscreen polygon can land just outside the frame; one
  // this far outside cannot, so this margin discards the numerically
  // unstable case without clipping ordinary geometry short.
  const RtFloat rasterMargin = (width > height ? width : height);

  for(int i=0; i<num_vert; i++) {

    vert = &(v_info[i]); // get vertex info element

    // Vertex position, already projected and perspective-divided by the
    // polygon clipper (GMANPolygonClipper::clip): NDC x,y in the screen
    // window's coordinate space, z ready for a linear depth test.
    posn = out.getVertexPosn(i);

    RtFloat x = posn.getX();
    RtFloat y = posn.getY();
    viewingSys->screenToRaster(x, y);
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < -rasterMargin || x > width + rasterMargin ||
        y < -rasterMargin || y > height + rasterMargin) {
      return false;
    }
    vert->posn.setX(x);
    vert->posn.setY(y);
    vert->posn.setZ(posn.getZ());

    // convert to screen coordinates
    vert->screen.x = (int)vert->posn.getX();
    vert->screen.y = (int)vert->posn.getY();

    // update y axis limits for polygon -- clamped to the framebuffer, so
    // scanEdges' edge_list[] walk (sized to `height`) never runs off the
    // end even if a vertex landed just inside the margin above but still
    // outside the visible frame.
    if(vert->screen.y < ymin)
      ymin = vert->screen.y;

    if(vert->screen.y > ymax)
      ymax = vert->screen.y;

    // Gouraud color: computeCi already ran once per tessellated vertex,
    // in camera space, with the real inputs a shader needs (P, N, I, the
    // active lights) -- createParametric (GMANPatchPolyObjectManager),
    // not here. A clip-introduced vertex has no u,v of its own to shade
    // with, which is why shading happens before clipping and this reads
    // the already-shaded, already-linearly-interpolated result rather
    // than calling computeCi per output vertex.
    vert->color = out.getVertexColor(i);

  }

  // Individual vertices can still land just outside the visible frame
  // (an ordinary partially-offscreen polygon); clamp the scan range that
  // indexes edge_list[] to what it was actually sized for.
  if (ymin < 0) ymin = 0;
  if (ymax > height - 1) ymax = height - 1;

  return true;
}

// scan polygon edges
void GMANZBufferRenderer::scanEdges(void) {
  int i, j;

  RtFloat	dx;		// x axis delta
  RtFloat	dz;		// pseudodepth delta
  RtFloat       ix;             // x intercept
  RtFloat       iz;             // pseudodepth intercept

  RtFloat       y_dist;		// y distance

  GMANColor     dc;		// intersection color delta
  GMANColor	ic;		// intersection color

  EdgeInfo	*edge;		// edge 
  ScanInfo	*scan;		// scan line

  VertexInfo	*sv;		// start vertex
  VertexInfo	*ev;		// end vertex

  VertexInfo	*sw;		// swap vertex info


  // initialize edge list
  for(i=ymin; i<=ymax; i++)
    edge_list[i].nisect = 0;


  for(i=0; i<num_vert; i++) {

    // get edge pointers
    sv = &(v_info[i]);
    ev = &(v_info[(i+1)%num_vert]);


    // FIXME: Not needed if polygon clipped
    if(sv->screen.y < 0 && ev->screen.y < 0) continue;
    if(sv->screen.y >= height && ev->screen.y >= height) continue;
    // END FIXME

    if(sv->screen.y == ev->screen.y)
      continue;

    if(sv->screen.y > ev->screen.y) {
      sw = sv; sv = ev; ev = sw;
    }

    // get start info
    ix = sv->posn.getX();
    iz = sv->posn.getZ();
    ic = sv->color;

    // determine inverse slopes
    y_dist = (RtFloat) (ev->screen.y - sv->screen.y);

    dx = (ev->posn.getX() - ix) / y_dist;
    dz = (ev->posn.getZ() - iz) / y_dist;

    // linearly interpolate color
    // FIXME FIXME FIXME
    // FIXME -- surface shader code here
    dc.setRed((ev->color.getRed() - sv->color.getRed()) / 
	      (GMANColorSample)y_dist);

    dc.setGreen((ev->color.getGreen() - sv->color.getGreen()) / 
	      (GMANColorSample)y_dist);

    dc.setBlue((ev->color.getBlue() - sv->color.getBlue()) / 
	      (GMANColorSample)y_dist);


    // scan convert edge
    edge = &(edge_list[sv->screen.y]);

    for(j=sv->screen.y; j< ev->screen.y; j++) {

      if (j >= 0 && j < height) { // FIXME: Not necessary if polygon is clipped
	// determine intersection info
	if(edge->nisect < 2) {
	  scan = &(edge->isect[edge->nisect++]);

	} else {
	  // A third crossing cannot occur for the convex polygons the
	  // clipper emits; overwrite isect[1] as this always did rather
	  // than run off the end of the array.
	  scan = &(edge->isect[1]);
	}

	// insert edge intersection info
	scan->x = ix;
	scan->z = iz;
	scan->color = ic;
      }	

      // update intersection info
      ix += dx;
      iz += dz;
      ic += dc;

      edge++; // go to next edge list element
    }

  }

}


// draw edge list into frame buffer
void GMANZBufferRenderer::drawEdgeList(GMANFrameBuffer *frameBuffer) {
  int x, y;
  int sx, ex;
  RtFloat dz;		// depth delta
  RtFloat iz;		// pixel depth
  RtFloat x_dist;	// x distance

  GMANColor	dc;	// color delta
  GMANColor	ic;	// pixel color

  EdgeInfo	*edge;	// edge info
  ScanInfo	*ss;    // scan line start info
  ScanInfo	*se;    // scan line end info
  ScanInfo      *sw;    // scan line swap

  edge = &(edge_list[ymin]);
  for(y = ymin; y<=ymax; y++) {

    // Fewer than two intersections is no span. y == ymax always lands
    // here, and reading its isect[] before this guard was the
    // uninitialized read valgrind caught -- one per polygon, which is
    // what made the rendered image nondeterministic.
    if(edge->nisect < 2) {
      edge++;
      continue;
    }

    ss = &(edge->isect[0]);
    se = &(edge->isect[1]);

    if(ss->x > se->x) {
      sw = ss; ss = se; se = sw;
    }

      // get scan line x coord
    sx = (int) ss->x;
    ex = (int) se->x;

    if(sx < ex) {

      iz = ss->z;
      ic = ss->color;

      // Determine inverse slopes. sx<ex (integer pixel columns) only
      // guarantees se->x>ss->x, not that the gap is anywhere near a
      // whole pixel: values straddling an integer boundary from just
      // below to just above (e.g. 1.999999 and 2.000001) round to
      // different columns but leave x_dist near zero, and dividing by
      // it below produces a color delta big enough to overflow the
      // eventual float-to-byte quantization. sx/ex already fix the
      // number of pixels this scanline covers, so a span narrower than
      // one pixel is clamped to one.
      x_dist = se->x - ss->x;
      if (x_dist < 1.0) x_dist = 1.0;

      dz = (se->z - iz) / x_dist;

      dc.setRed((se->color.getRed() -
		 ss->color.getRed()) / (GMANColorSample) x_dist);

      dc.setGreen((se->color.getGreen() -
		   ss->color.getGreen()) / (GMANColorSample) x_dist);

      dc.setBlue((se->color.getBlue() -
		  ss->color.getBlue()) / (GMANColorSample) x_dist);

      
      // Gouraud shade scan line
      for(x=sx; x <= ex; x++) {

	if (x >= 0 && x < width) { // FIXME: poly should already be clipped?

	  // test zbuffer
	  if(iz < getZBuffer(x, y)) {
	    // it's closer
	    // set new z buffer depth
	    setZBuffer(x, y, iz);

	    // Linear interpolation can overshoot [0,1] by a small amount at
	    // the far end of a span (accumulated float error over many
	    // += dc steps); GMANColorRGB's float->byte conversion has no
	    // clamp of its own, so an unclamped color here is UB, not just
	    // a visibly wrong pixel.
	    GMANColor clamped(GMANClamp<GMANColorSample>(ic.getRed(), 0.0, 1.0),
			       GMANClamp<GMANColorSample>(ic.getGreen(), 0.0, 1.0),
			       GMANClamp<GMANColorSample>(ic.getBlue(), 0.0, 1.0));
	    frameBuffer->setPixel(x, y, clamped);
	  }

	}

	// update pixel info
	iz += dz;
	ic += dc;

      }
    }

    edge++;
  }
}


RtVoid GMANZBufferRenderer::render(GMANFrameBuffer    *frameBuffer,
				   GMANViewingSystem  *viewingSys,
				   const GMANOptions       &/*options*/,
				   const GMANAttributes    &/*attributes*/)
 {

  GMANFace	*face;
  GMANSurface   *surf;
  GMANBody      *body;
  GMANOutputPolygon		outPoly;

  width = frameBuffer->getWidth();
  height = frameBuffer->getHeight();

  this->viewingSys = viewingSys;

  initZBuffer();

  debug("GMANZBufferRenderer::render");

  // render each object
  GMANPrimitive* primitive = worldManager.getFirst();
  while(primitive) {

    GMANObject *object = dynamic_cast<GMANObject*>(primitive);

    body = object->getBody();
    while(body) {
      surf = body->getSurface();
      while(surf) {
  	  face = surf->getFace();
	  while(face) {
	    // do back face culling
	    if(viewingSys->visible(face)) {
	      outPoly.reset();
	      // Projects each vertex through viewingSys->getProjMatrix(),
	      // clips in homogeneous space against the 6-plane frustum,
	      // and perspective-divides the surviving vertices into outPoly
	      // -- so near-plane clipping happens here, before rasterization
	      // ever sees a point behind the camera.
	      clipper.clip(face, outPoly, viewingSys);

	      //render the face into frameBuffer
	      if (outPoly.getNumVert() > 0) {
		render(outPoly, frameBuffer);
	      }
	    }

	    face = face->getNext();
  	  }
	surf = surf->getNext();
      }
      body = body->getNext();
    }
    primitive = worldManager.getNext();
  }
}

// world manager
GMANWorldManager *GMANZBufferRenderer::getWorldManager(void) {
  return &worldManager;
}

// return its object manager
GMANObjectManager *GMANZBufferRenderer::getObjectManager(void) {
  return &objectManager;
}

RtVoid GMANZBufferRenderer::setZBuffer(RtInt x, RtInt y, RtFloat val) {
  zbuffer[y*width + x] = val;
}

RtFloat GMANZBufferRenderer::getZBuffer(RtInt x, RtInt y) const {
  return zbuffer[y*width + x];
}
