/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2002, 2001, 2000, 1999  John Cairns 
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
#include "gmanreyesrenderer.h" /* Declaration Header */
#include "gmanpolygonclipper.h"

GMANReyesRenderer::GMANReyesRenderer()  {
    zBuffer=NULL;
    firstHit=NULL;
}

GMANReyesRenderer::~GMANReyesRenderer() {
    if(zBuffer) delete []zBuffer;
    if(firstHit) delete []firstHit;
}


void GMANReyesRenderer::render(GMANFrameBuffer *frameBuffer,
			       GMANViewingSystem *viewingSys,
			       const GMANOptions       &options,
			       const GMANAttributes    &attributes)
    throw (GMANError) {

  width = frameBuffer->getWidth();
  height = frameBuffer->getHeight();

  GMANPolygonClipper   clipper;

    GMANFace	*face;
    GMANSurface   *surf;
    GMANBody      *body;
    GMANOutputPolygon		outPoly;

    // REYES PORTION
    debug("Performing REYES calculation");

    // for each objects bodies surfaces faces...
    GMANObject *object = dynamic_cast<GMANObject*>(worldManager.getFirst());
    while(object) {
	body = object->getBody();
	while(body) {
	    surf = body->getSurface();
	    while(surf) {
		face = surf->getFace();
		while(face) {
		    if(viewingSys->visible(face)) {
			outPoly.reset();
			for (int i = 0; i < face->getNumVerts(); i++) {
			    const GMANVertex *fv = face->getVertex(i);
			    
			    GMANPoint p = fv->getLocation();

			    GMANVertex4 v;
			    GMANVector4 coords(p.getX(), 
					       p.getY(), 
					       p.getZ(), 1.0);
			    
			    v.set(coords, fv->getColor(), fv->getAlpha());
			    outPoly.addVertex(v);
			}
			(void)clipper.clip(face, 
					   outPoly, 
					   viewingSys);
	    
			//render the face into frameBuffer
			// using the reyes alg.
			reyes(outPoly, frameBuffer);
			
			face = face->getNext();
		    }
		}
		surf = surf->getNext();
	    }      
	    body = body->getNext();
	}
	object = dynamic_cast<GMANObject*>(worldManager.getNext());
    }

    debug("REYES portion done.");

    debug("Beginning ray casting portion.");
    
    // We have applied a local illumination model to 'hopefully'
    // most of the image ... now we want to raytrace the image in
    // screen space
    for(int xs=0; xs < width; xs++) {
	for(int ys=0; ys < height; ys++) {
	    
	    // should we cast a ray ?
	    if(checkSpec(xs, ys) ||  // yes if specular
	       checkTrans(xs, ys)) { // yes if transparancy
		traceRay(xs, ys, frameBuffer, viewingSys);
	    };
	}
    }
    debug("Ray casting done.");
}

// scan the polygon with reyes :)
void GMANReyesRenderer::reyes(GMANOutputPolygon &out,
			      GMANFrameBuffer    *frameBuffer) 
    throw (GMANError) {
    
}

    
// trace a ray from screen x and screen y
void GMANReyesRenderer::traceRay(RtInt xs, 
				 RtInt ys,
				 GMANFrameBuffer    *frameBuffer,
				 GMANViewingSystem  *viewingSystem) 
    throw (GMANError) {

    

}

// return true if the specified point is specular
bool GMANReyesRenderer::checkSpec(RtInt xs, RtInt ys) {
  return false; // for now we don't check
}
  // return true if the specified point is transparent
bool GMANReyesRenderer::checkTrans(RtInt xs, RtInt ys) {
  return false; // for now we don't check
}
