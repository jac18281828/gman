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

#include <typeinfo>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
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
};
GMANZBufferRenderer::GMANZBufferRenderer() : 
  GMANRenderer(), width(0), height(0) {
  zbuffer=NULL;
  edge_list=NULL;
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
      edge_list = new EdgeInfo[height];
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
  throw (GMANError) {
  
  getVertexInfo(out);
  scanEdges();
  drawEdgeList(frameBuffer);

};


// Get vertex info
void GMANZBufferRenderer::getVertexInfo(GMANOutputPolygon &out) {

  VertexInfo    *vert;
  GMANPoint	posn;

  // y axis
  ymax = 0;
  ymin = height - 1;

  num_vert = out.getNumVert();

  for(int i=0; i<num_vert; i++) {

    vert = &(v_info[i]); // get vertex info element

    // Get vertex normalized view space coordinates
    posn = out.getVertexPosn(i);

    // FIXME: hardcoded (-1,1),(-1,1) view plane window
    vert->posn.setX((width / 2.0) + posn.getX() * (height / 2.0));
    vert->posn.setY((height / 2.0) - posn.getY() * (height / 2.0));
    vert->posn.setZ(posn.getZ());

    // convert to screen coordinates
    vert->screen.x = (int)vert->posn.getX();
    vert->screen.y = (int)vert->posn.getY();

    // update y axis limits for polygon
    if(vert->screen.y < ymin)
      ymin = vert->screen.y;

    if(vert->screen.y > ymax)
      ymax = vert->screen.y;

    // get color
    //    vert->color = out.getVertexColor(i);
    vert->color =
      GMANColor((float) drand48(), (float) drand48(), (float) drand48());

  }
  // FIXME: Polygon should be clipped
  if (ymin < 0) ymin = 0;
  if (ymax > height - 1) ymax = height - 1;
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
    edge_list[i].first = false;


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
	if(edge->first == false) {
	  scan = &(edge->isect[0]);
	  edge->first = true;
	  
	} else {
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
      
      // Determine inverse slopes
      x_dist = se->x - ss->x;
      
      
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
	    
	    frameBuffer->setPixel(x, y, ic);
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
				   const GMANOptions       &options,
				   const GMANAttributes    &attributes)
    throw (GMANError) {

  GMANFace	*face;
  GMANSurface   *surf;
  GMANBody      *body;
  GMANOutputPolygon		outPoly;

  width = frameBuffer->getWidth();
  height = frameBuffer->getHeight();

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
	      for (int i = 0; i < face->getNumVerts(); i++) {
		const GMANVertex *fv = face->getVertex(i);
		GMANPoint p = fv->getLocation();
		GMANVertex4 v;
		GMANVector4 coords(p.getX(), p.getY(), p.getZ(), 1.0);
		v.set(coords, fv->getColor(), fv->getAlpha());
		outPoly.addVertex(v);
	      }
	      
	      //render the face into frameBuffer
	      render(outPoly, frameBuffer);
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
